#pragma once
#include "string"

struct app_arguments
{
    // ͨ�ò���
    std::string input = ""; // unused
    std::string output = "";

    // ѹ��
    bool pack = false;
    std::string pack_src = "";
    bool pack_no_hash = false;
    std::string pack_exec = "";

    // ��ѹ
    bool extract = false;
    //std::string extract_dist = "";

    // ����
    bool detail = false;

    // ����
    bool optarg_required = false;
    bool unknown_opt = false;
    std::string invaild_opt_name = "";

    // ����
    bool help = false;
};

app_arguments parse_args(int argc, char** argv);