#pragma once

// WINDOW_MODE��Ӱ��������������ں�������������consles���ں�һ���ò�������ˣ�
//#define WINDOW_MODE

#define ENABLED_ERROR_CHECK

#define PROJECT_NAME "LittleWrapper"
#define VERSION_TEXT "1.0.1"

#define PATH_MAX 256

#define MAGIC_HEADER "0123456789abcdefghijkmnlopqrtsuvwxyz|"
#define MAGIC_LEN (sizeof(MAGIC_HEADER) / sizeof(char) - 1)
#define PRESERVE_LEN 1024

/*
1.0.1: 2021��7��2��: 
  1. ��ͬ�������������exitcode
  2. ����lasterror��Ϣ��ʾ
  3. ���Ӿ���·����֧��

*/