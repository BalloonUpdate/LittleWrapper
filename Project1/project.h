#pragma once

// WINDOW_MODE��Ӱ��������������ں�������������consles���ں�һ���ò�������ˣ�
//#define WINDOW_MODE

#define ENABLED_ERROR_CHECK

#define PROJECT_NAME "LittleWrapper"
#define VERSION_TEXT "1.0.5"
#define PROJ_VER PROJECT_NAME " v" VERSION_TEXT

#define PATH_MAX 256

#define MAGIC_HEADER "0123456789abcdefghijkmnlopqrtsuvwxyz|"
#define MAGIC_LEN (sizeof(MAGIC_HEADER) / sizeof(char) - 1)
#define PRESERVE_LEN 1024

/*
1.0.1: 2021��7��2��: 
  1. ��ͬ�������������exitcode
  2. ����lasterror��Ϣ��ʾ
  3. ���Ӿ���·����֧��

1.0.2: 2021��7��5��:
  1. ʹ��·��MD5������ʱ�ļ������ó�ͻ
  2. ��д����������ش���
  3. ʹ��command��������_start.txt�ļ�

1.0.3: 2021��7��5��:
  1. ����һЩ������Ϣ���ı�����
  2. �޸�����Ϊ����ļ��Զ�������Ŀ¼������
  3. �Ӵ�error_check()�Ļ�������С

1.0.4: 2021��7��8��
  1. �޸����ʱ--output��������ֱ�����ļ���������
  2. ���ӳ־���ʾconsole��ѡ��

1.0.5: 2021��7��15��
  1. �޸��ļ�У���жϴ�������
  2. �Ż������ı������
  3. ֧��ʹ�����������ô����Ƿ�����
  4. ������ʱĿ¼��·��

*/