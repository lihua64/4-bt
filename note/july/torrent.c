#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <errno.h>
#include "torrent.h"
#include "message.h"
#include "tracker.h"
#include "peer.h"
#include "policy.h"
#include "data.h"
#include "bitfield.h"
#include "parse_metafile.h"

// ���ջ������е����ݴﵽthresholdʱ,��Ҫ�������д���,���򻺳������ܻ����
// 18*1024��18K�ǽ��ջ������Ĵ�С,1500����̫���Ⱦ�����һ�����ݰ�����󳤶�
#define  threshold (18*1024-1500)

extern Announce_list *announce_list_head;//�ñ�����������tracker������
extern char          *file_name; 
extern long long     file_length;
extern int           piece_length;
extern char          *pieces;
extern int           pieces_length;
extern Peer          *peer_head;

extern long long     total_down,total_up;
extern float         total_down_rate,total_up_rate;
extern int           total_peers;
extern int           download_piece_num;
extern Peer_addr     *peer_addr_head; //�洢�˴ӷ��������ص�����Peer��ip��ַ�Ͷ˿ں�

int                  *sock    = NULL;  //�洢��tracker����ʱ���׽���
struct sockaddr_in   *tracker = NULL; //�洢��������ip��ַ�Ͷ˿ں�,��������ʱǿ��ת��Ϊsockaddr���͵ı���
int                  *valid   = NULL; //��ʱ������״̬
int                  tracker_count  = 0;

int                  response_len   = 0;//������
int                  response_index = 0;
char                 *tracker_response = NULL;

int                  *peer_sock  = NULL; //�洢��Peer���ӵ��׽���
struct sockaddr_in   *peer_addr  = NULL;
int                  *peer_valid = NULL;
int                  peer_count  = 0;

// ����������Peer�շ����ݡ�������Ϣ
int download_upload_with_peers()
{
	Peer            *p;
	int             ret, max_sockfd, i;

	int             connect_tracker, connecting_tracker;
	int             connect_peer, connecting_peer;
	time_t          last_time[3], now_time;

	time_t          start_connect_tracker;  // ��ʼ����tracker��ʱ��
	time_t          start_connect_peer;     // ��ʼ����peer��ʱ��
	fd_set          rset, wset;  // selectҪ���ӵ�����������
	struct timeval  tmval;       // select�����ĳ�ʱʱ��

	
	now_time     = time(NULL);
	last_time[0] = now_time;   // ��һ��ѡ�������peer��ʱ��
	last_time[1] = now_time;   // ��һ��ѡ���Ż�������peer��ʱ��
	last_time[2] = now_time;   // ��һ������tracker��������ʱ��
	/**
	�������������ӷ�������
	**/
	connect_tracker    = 1;    // �Ƿ���Ҫ����tracker
	connecting_tracker = 0;    // �Ƿ���������tracker
	connect_peer       = 0;    // �Ƿ���Ҫ����peer 
	connecting_peer    = 0;    // �Ƿ���������peer

	for(;;) {
		max_sockfd = 0;
		now_time = time(NULL);
		
		// ÿ��10������ѡ�������peer
		if(now_time-last_time[0] >= 10) {
			//ΪʲôҪdownload_piece_num > 0��
			if(download_piece_num > 0 && peer_head != NULL) {
				compute_rate();         // �������peer�����ء��ϴ��ٶ�
				select_unchoke_peer();  // ѡ���������peer
				last_time[0] = now_time;
			}
		}
		
		// ÿ��30������ѡ���Ż�������peer
		if(now_time-last_time[1] >= 30) {
			if(download_piece_num > 0 && peer_head != NULL) {
				select_optunchoke_peer();//���ѡ���������peer
				last_time[1] = now_time;
			}
		}
		
		// ÿ��5��������һ��tracker,�����ǰpeer��Ϊ0Ҳ����tracker
		if((now_time-last_time[2] >= 300 || (connect_tracker == 1 && 
			connecting_tracker != 1 && connect_peer != 1 && connecting_peer != 1) {
			// ��tracker��URL��ȡtracker��IP��ַ�Ͷ˿ں�
			ret = prepare_connect_tracker(&max_sockfd);
			if(ret < 0)  { printf("prepare_connect_tracker\n"); return -1; }

			connect_tracker       = 0;
			connecting_tracker    = 1;//��ȡtracker����������Ϣ֮���������ʼ���ӷ�����
			start_connect_tracker = now_time;
		}
		
		/**��ʼ���ӷ�������˳��������connect_tracker,connecting_tracker,connect_peer,connecting_peer
		connecting_tracker����֮��,���Peer_addr������Ϊ��,����Ҫ�������ӷ�����,��Ϊ��ʱ��û��Peer���Խ�������
		���Peer�ṹ����������ݲ�Ϊ�գ���ô��������������tracker������������Ļ���Ҫ�������Ӹ÷�����
		*/
		if(connect_peer == 1) {
			// �����׽���,��peer������������
			ret = prepare_connect_peer(&max_sockfd);
			if(ret < 0)  { printf("prepare_connect_peer\n"); return -1; }

			connect_peer       = 0;
			connecting_peer    = 1;
			start_connect_peer = now_time;
		}
		//�������ļ�������������,����������Է����ļ��������ֵ�sock���м���������ͬʱ�Է��������ߵ��������ӵ�peer��������  
		//ͨ�ŵ�peer���м���
		FD_ZERO(&rset);
		FD_ZERO(&wset);//���ļ�������������,�������������ַ���select֮��,��select�����˳���ʱ�򣬻��ÿ��λ��������ʱ��״̬�������á�
					   //��ˣ������ѭ����ʹ��select,����ļ����������ڵ�����ǰÿ�ζ���Ҫ���г�ʼ��

		// ������tracker��socket���뵽�����ӵļ�����
		if(connecting_tracker == 1) {
			int flag = 1;
			//�������trackerʱ�䳬��10��,����ֹ����tracker
			if(now_time-start_connect_tracker > 10) {
				for(i = 0; i < tracker_count; i++)
					if(valid[i] != 0)  close(sock[i]);
			} else {
				for(i = 0; i < tracker_count; i++) {
					if(valid[i] != 0 && sock[i] > max_sockfd)
						max_sockfd = sock[i];  // valid[i]ֵΪ-1��1��2ʱҪ����
					if(valid[i] == -1) { 
						FD_SET(sock[i],&rset); //��valid[i] == -1ʱ�����Ӳ�һ��ʧ�ܣ�ֻ��û�������������,����Ҫ������Ӧ���ж�
						FD_SET(sock[i],&wset);
						if(flag == 1)  flag = 0;
					} else if(valid[i] == 1) {
						FD_SET(sock[i],&wset);
						if(flag == 1)  flag = 0;
					} else if(valid[i] == 2) {
						FD_SET(sock[i],&rset);
						if(flag == 1)  flag = 0;
					}
				}
			}
			// ���flag = 1,˵������tracker����,��ʼ��peer��������
			if(flag == 1) {
				connecting_tracker = 0; //���ӷ�������ʽ����
				last_time[2] = now_time;
				clear_connect_tracker();
				clear_tracker_response();
				//������trackerû�л�ȡ��Peer����Ϣʱ�����޷�����ͨ��connect_peer������Peer������������tracker������
				if(peer_addr_head != NULL) { 
					connect_tracker = 0;
					connect_peer    = 1;
				} else { 
					connect_tracker = 1;  
				}
				continue;
			}
		}

		//����û�гɹ����ӵ�peer���뵽���������н��м���
		if(connecting_peer == 1) {
			int flag = 1;
			// �������peer����10��,����ֹ����peer		
			if(now_time-start_connect_peer > 10) {
				for(i = 0; i < peer_count; i++) {
					if(peer_valid[i] != 1) close(peer_sock[i]);  //����10s,valid[i]��Ϊ1˵������ʧ��
				}
			} else {
				for(i = 0; i < peer_count; i++) {
					if(peer_valid[i] == -1) { //���ｫvalid[i] == -1��ֵ������м�������Ϊ�˽���Щ���ӽ����жϣ��Ƿ�ɹ�����
												//�������Ѿ����ӳɹ��Ľ�ͨ������ѭ�������ļ�����������
						if(peer_sock[i] > max_sockfd)
							max_sockfd = peer_sock[i];
						FD_SET(peer_sock[i],&rset); 
						FD_SET(peer_sock[i],&wset);
						if(flag == 1)  flag = 0;
					}
				}
			}
			//��flag = 1ʱ,����Peer����
			if(flag == 1) {
				connecting_peer = 0;
				clear_connect_peer();
				if(peer_head == NULL)  connect_tracker = 1;  //��������ʱ���Peer����Ϊ�գ��������ӷ�������ȡ����
				continue;
			}
		}
		
		//ÿ��3���ӻ�����һ�η�����,��3����֮��,����������е����Ӷ����ر���,��ô�������������ӷ�����
	   //�����connect_tracker��Ϊ1,���������ж��Ƿ���Ҫ�������ӷ�����
		connect_tracker = 1; 
		
		//�����������,���ǲ�û��ʵ�ּ���ĳ���˿�,���Ǵ�ʱ��Ҫ�����Ѿ�ʵ����
		//���������,������µ�peer�����ǽ�������,��ô��ʱ����Ҫ�������ӷ����µ��׽���,���������������,�������ӵ��׽��ֻ�Ͽ�����
	    //����ÿ�ζ��Ὣ�µ��׽��ַ�������������
		p = peer_head;
		while(p != NULL) {
			if(p->state != CLOSING && p->socket > 0) {
				FD_SET(p->socket,&rset); 
				FD_SET(p->socket,&wset); 
				if(p->socket > max_sockfd)  max_sockfd = p->socket; 
				connect_tracker = 0;
			}
			p = p->next;
		}
		//�ж��Ƿ�Ҫ���ӷ�����
		if(peer_head==NULL && (connecting_tracker==1 || connecting_peer==1)) 
			connect_tracker = 0; //���������û��������peerҲû��������tracker����������Peer����ҲΪ�յĻ�������Ҫ���ӷ�������
		if(connect_tracker == 1)  continue;

		tmval.tv_sec  = 2;
		tmval.tv_usec = 0;//���õȴ�ʱ��
		ret = select(max_sockfd+1,&rset,&wset,NULL,&tmval);//select�ĵ�һ������nfds��Ӧ������Ϊ�������ļ����������������ļ���������1
	    //���ִ���	
	   if(ret < 0)  { 
			printf("%s:%d  error\n",__FILE__,__LINE__);
			perror("select error"); 
			break;
		}
		//�����ʱ����û���κ����鷢���������еľ����ɶ���д
		if(ret == 0)  continue;

		// ���have��Ϣ,have��ϢҪ���͸�ÿһ��peer,���ڴ˴���Ϊ�˷��㴦��
		prepare_send_have_msg();

		// ����ÿ��peer,���ջ�����Ϣ,����һ����������Ϣ�ͽ��д���
		//�����ǰ������
		p = peer_head;
		while(p != NULL) {
			if( p->state != CLOSING && FD_ISSET(p->socket,&rset) ) {
				ret = recv(p->socket,p->in_buff+p->buff_len,MSG_SIZE-p->buff_len,0);//�Ӹ��׽��ֽ�����Ϣ����ŵ�p->in_buff��
				if(ret <= 0) {  // recv����0˵���Է��ر�����,���ظ���˵������
					//if(ret < 0)  perror("recv error"); 
					p->state = CLOSING; //������ΪCLOSING�����򽫻Ὣ��peer��������ɾ���������ٽ��ջ��߷�����Ϣ
					
					// ͨ�������׽���ѡ�����������ͻ������е�����
					discard_send_buffer(p);
					clear_btcache_before_peer_close(p);
					close(p->socket); 
				} else {
					//������������������Ϣʱ,�����p->buff_len��ֵ��������
					int completed, ok_len;
					p->buff_len += ret;
					completed = is_complete_message(p->in_buff,p->buff_len,&ok_len);//�ж���p->in_buff�������Ƿ�Ϊһ����������Ϣ
					
					if (completed == 1)  parse_response(p);
					//����������Ϣ����threshold,���Ͻ��д���
					else if(p->buff_len >= threshold) {
						parse_response_uncomplete_msg(p,ok_len);//������������Ϣ
					} else {
						p->start_timestamp = time(NULL);//��¼���һ�ν���peer��Ϣ��ʱ��
					}
				}
			}
			//��ʱ��û�йر�����,���Ҵ�ʱ�ǿ�д��״̬
			if(p->state != CLOSING && FD_ISSET(p->socket,&wset) ) {
				//��û��Ҫ���͸�peer����Ϣʱ,�������ݴ�ʱ��״̬������Ϣ
				if( p->msg_copy_len == 0) {
					// ���������͵���Ϣ,�������ɵ���Ϣ���������ͻ�����������
					create_response_message(p);
					if(p->msg_len > 0) {
						memcpy(p->out_msg_copy,p->out_msg,p->msg_len);//��Ҫ���͵���Ϣ���Ƶ���������
						p->msg_copy_len = p->msg_len;
						p->msg_len = 0; // ��Ϣ���ȸ�0,ʹp->out_msg������Ϣ���
					}	
				}	
				
				if(p->msg_copy_len > 1024) {				
				//���ͳ���Ϊ1024����Ϣ��peer,һ�㶼�ǿ���ȫ�����͹�ȥ��,����������ǳ�������޷����͵����ջ�����,�Ż����socket�������޷�ȫ��װ�·��͵���Ϣ
					send(p->socket,p->out_msg_copy+p->msg_copy_index,1024,0);
					p->msg_copy_len   = p->msg_copy_len - 1024;//��¼��ʱmsg_copyʣ������ݵĳ���
					p->msg_copy_index = p->msg_copy_index + 1024;//��¼�´η������ݵ���ʼ����
					p->recet_timestamp = time(NULL); // ��¼���һ�η�����Ϣ��peer��ʱ��
				}
				else if(p->msg_copy_len <= 1024 && p->msg_copy_len > 0 ) {
					send(p->socket,p->out_msg_copy+p->msg_copy_index,p->msg_copy_len,0);//������������Ϣȫ�����͸����Ӧ��peer
					p->msg_copy_len   = 0;
					p->msg_copy_index = 0;
					p->recet_timestamp = time(NULL); // ��¼���һ�η�����Ϣ��peer��ʱ��
				}
			}
			p = p->next;
		}
		/**
		�����Ƕ��������ӵ�tracker��ͨ�Ŵ���
		*/
		if(connecting_tracker == 1) {
			for(i = 0; i < tracker_count; i++) {
				if(valid[i] == -1) {
					// ���ĳ���׽��ֿ�д��δ��������,˵�����ӽ����ɹ�
					//���error���ص���EINPPROGRESS�Ļ����ʹ�������߳�û�б�����
					//�����أ��������Ҳ���ܱ�������ɡ�
					//������ʹ��poll����select��������ӡ�����select��,�������׽����ǿɶ��Ļ�
					//�����������getsockoptȥ��SO_ERROR��������������Ƿ񹦣����SO_ERROR = 0 �Ļ��ʹ���ɹ�
					//��Ȼ�Ļ�����ʧ����
					if(FD_ISSET(sock[i],&wset)) {
						int error, len;
						error = 0;
						len = sizeof(error);
						if(getsockopt(sock[i],SOL_SOCKET,SO_ERROR,&error,&len) < 0) {
							valid[i] = 0; 
							close(sock[i]);
						}
						if(error) { valid[i] = 0; close(sock[i]); } 
						else { valid[i] = 1; }
					}
				}
				//ʵ�ּ����ö˿�,���ܳɹ�����connect���͹���������,���ҿ��Գɹ��뷢��connect�����Peer����ͨ��
				//����������в�û��ʵ�ּ����ö˿�
				if(valid[i] == 1 && FD_ISSET(sock[i],&wset) ) {
					char  request[1024];
					unsigned short listen_port = 33550; // ������δʵ�ּ���ĳ�˿�
					unsigned long  down = total_down;
					unsigned long  up = total_up;
					unsigned long  left;
					left = (pieces_length/20-download_piece_num)*piece_length;
					
					int num = i;
					Announce_list *anouce = announce_list_head;
					while(num > 0) {
						anouce = anouce->next;
						num--;
					}
					create_request(request,1024,anouce,listen_port,down,up,left,200);//����������Ϣ
					write(sock[i], request, strlen(request));//�������������Ϣ,�ɹ����͵Ļ����ڷ�����ģʽ�·���ֵΪ1��strlen(request);
					valid[i] = 2;
				}
				//�ⲿ�ֵ����ݲ��Ǻ����,������ΪʲôҪ�����ӽ������������͹�������Ϣ
				if(valid[i] == 2 && FD_ISSET(sock[i],&rset)) {
					char  buffer[2048];
					char  redirection[128];  //���ڴ洢�������ض����url
					ret = read(sock[i], buffer, sizeof(buffer));//���շ��������͹�������Ϣ,�ɹ���ȡ����������Ϊ��ȡ���ֽ���
					//��������Ƿ���Բ²��������Ӧ��Ϣ�ĳ��Ȳ�����2048,�ʿ��Խ����������͹�������Ϣȫ������,
					//���response_len=0,�����ڴ�,��������ڴ����ܶ�ȡ����������֮��,�㽫��Ϣ���н���,���Ҷ��ǵڶ������͵Ľ���
					if(ret > 0)  {
						if(response_len != 0) {
							memcpy(tracker_response+response_index,buffer,ret);
							response_index += ret;
							if(response_index == response_len) {
								parse_tracker_response2(tracker_response,response_len);
								clear_tracker_response();//��tracker_response��ռ���ڴ�����,response_len = 0,response_index = 0
								valid[i] = 0;
								close(sock[i]);//��Ϊʲôֻ�ر����������?
								last_time[2] = time(NULL);//�������һ������������ӵ�ʱ��
							}
						}
					    /**
						���յ������ǵ�һ�����͵�����,����ŵ�����������
						*/
						else if(get_response_type(buffer,ret,&response_len) == 1) {
							tracker_response = (char *)malloc(response_len);//�����ڴ�洢���������͹�������Ϣ
							if(tracker_response == NULL)
							{
								printf("malloc error\n");
								return -1;
							}
							memcpy(tracker_response,buffer,ret);
							response_index = ret;
						} else {
							//���е�һ��������Ϣ�Ľ���,��������˵�һ�ֽ���,��ô�㲻�ò��ܽ��еڶ��ν�������
							ret = parse_tracker_response1(buffer,ret,redirection,128);
							if(ret == 1) add_an_announce(redirection);//���һ���µ�url
							valid[i] = 0;
							close(sock[i]);//�ر��������
							last_time[2] = time(NULL);
						}
					}
				} 
			}
		}
		//����û�гɹ����ӵ�peer���м��,��������������trackerʱ����ͬ
		if(connecting_peer == 1) {
			for(i = 0; i < peer_count; i++) {
				if(peer_valid[i] == -1 && FD_ISSET(peer_sock[i],&wset)) {
					int error, len;
					error = 0;
					len = sizeof(error);
					if(getsockopt(peer_sock[i],SOL_SOCKET,SO_ERROR,&error,&len) < 0) {
						peer_valid[i] = 0;
					}
					if(error == 0) {
						peer_valid[i] = 1;
						add_peer_node_to_peerlist(&peer_sock[i],peer_addr[i]);//���ӳɹ�ֱ�ӽ���ŵ�Peer������ȥ
					}
				} // end if
			} // end for
		} // end if
		
		// �Դ���CLOSING״̬��peer,�����peer������ɾ��
		// �˴�Ӧ���ǳ�С��,�������ǳ�����ʹ�������
		p = peer_head;
		while(p != NULL) {
			if(p->state == CLOSING) {
				del_peer_node(p); 
				p = peer_head;
			} else {
				p = p->next;
			}
		}

		// �ж��Ƿ��Ѿ��������
		if(download_piece_num == pieces_length/20) { 
			printf("++++++ All Files Downloaded Successfully +++++\n"); 
			break;
		}
	}

	return 0;
}

void print_process_info()
{
	char  info[256];
	float down_rate, up_rate, percent;
	
	down_rate = total_down_rate;
	up_rate   = total_up_rate;
	percent   = (float)download_piece_num / (pieces_length/20) * 100;
	if(down_rate >= 1024)  down_rate /= 1024;
	if(up_rate >= 1024)    up_rate   /= 1024;
	
	if(total_down_rate >= 1024 && total_up_rate >= 1024)
		sprintf(info,"Complete:%.2f%% Peers:%d Down:%.2fKB/s Up:%.2fKB/s \n",
				percent,total_peers,down_rate,up_rate);
	else if(total_down_rate >= 1024 && total_up_rate < 1024)
		sprintf(info,"Complete:%.2f%% Peers:%d Down:%.2fKB/s Up:%.2fB/s \n",
				percent,total_peers,down_rate,up_rate);
	else if(total_down_rate < 1024 && total_up_rate >= 1024)
		sprintf(info,"Complete:%.2f%% Peers:%d Down:%.2fB/s Up:%.2fKB/s \n",
				percent,total_peers,down_rate,up_rate);
	else if(total_down_rate < 1024 && total_up_rate < 1024)
		sprintf(info,"Complete:%.2f%% Peers:%d Down:%.2fB/s Up:%.2fB/s \n",
				percent,total_peers,down_rate,up_rate);
	
	//if(total_down_rate<1 && total_up_rate<1)  return;
	printf("%s",info);
}

int print_peer_list()
{
	Peer *p = peer_head;
	int  count = 0;
	
	while(p != NULL) {
		count++;
		printf("IP:%-16s Port:%-6d Socket:%-4d\n",p->ip,p->port,p->socket);
		p = p->next;
	}
	
	return count;
}

void release_memory_in_torrent()
{
	if(sock    != NULL)  { free(sock);    sock = NULL; }
	if(tracker != NULL)  { free(tracker); tracker = NULL; }
	if(valid   != NULL)  { free(valid);   valid = NULL; }

	if(peer_sock  != NULL)  { free(peer_sock);  peer_sock  = NULL; }
	if(peer_addr  != NULL)  { free(peer_addr);  peer_addr  = NULL; }
	if(peer_valid != NULL)  { free(peer_valid); peer_valid = NULL; }
	free_peer_addr_head();
}

void clear_connect_tracker()
{
	if(sock    != NULL)  { free(sock);    sock    = NULL; }
	if(tracker != NULL)  { free(tracker); tracker = NULL; }
	if(valid   != NULL)  { free(valid);   valid   = NULL; }
	tracker_count = 0;
}

void clear_connect_peer()
{
	if(peer_sock  != NULL) { free(peer_sock);  peer_sock  = NULL; }
	if(peer_addr  != NULL) { free(peer_addr);  peer_addr  = NULL; }
	if(peer_valid != NULL) { free(peer_valid); peer_valid = NULL; }
	peer_count = 0;
}

void clear_tracker_response()
{
	if(tracker_response != NULL) { 
		free(tracker_response);
		tracker_response = NULL;
	}
	response_len   = 0;
	response_index = 0;
}