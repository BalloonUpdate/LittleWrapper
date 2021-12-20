#pragma once
#include "../libs/cJSON-1.7.14/cJSON.h"
#include <iostream>
#include <vector>

struct file_struct_t
{
    bool is_file;
    bool read_only;
    bool hidden;

    uint64_t create;
    uint64_t access;
    uint64_t write;

    std::string name;
    uint64_t length;

    std::vector<file_struct_t> children;
};

/// <summary>
/// �����ļ��нṹ
/// </summary>
/// <param name="path">Ҫ���ɵ�·��</param>
/// <returns>�ļ��нṹ��Ϣ</returns>
cJSON* dirstruct_to_jsonlist(std::vector<file_struct_t> file_struct);

/// <summary>
/// �����ļ��мܹ�
/// </summary>
/// <param name="path">Ҫ���ɵ�·��</param>
/// <returns>�ļ��б�</returns>
std::vector<file_struct_t> generate_dir_struct(std::string path);