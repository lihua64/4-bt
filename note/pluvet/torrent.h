//torrent.h
#ifndef TORRENT_H
#define TORRENT_H

#include "tracker.h"

// ����������Peer�շ����ݡ�������Ϣ
int download_upload_with_peers();

int  print_peer_list();	//��ӡpeer�����и���peer��IP�Ͷ˿ں�
void print_process_info();	//��ӡ���ؽ�����Ϣ

void clear_connect_tracker();	//�ͷ�������Tracker�йص�һЩ��̬�洢�ռ�
void clear_connect_peer();	//�ͷ�������peer�йص�һЩ��̬�洢�ռ�
void clear_tracker_response();//�ͷ������Tracker��Ӧ�йص�һЩ��̬�洢�ռ�
void release_memory_in_torrent();//�ͷ�torrent.c�ж�̬����Ĵ洢�ռ�

#endif