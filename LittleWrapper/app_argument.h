#pragma once
#include <string>

struct app_args
{
    // 通用参数
    std::string input = ""; // unused
    std::string output = "";

    // 压缩
    bool pack = false;
    std::string pack_src = "";
    bool pack_no_hash = false;
    std::string pack_exec = "";

    // 解压
    bool extract = false;
    std::string extract_dest = "";

    // 详情
    bool detail = false;

    // 错误
    bool optarg_required = false;
    bool unknown_opt = false;
    std::string invaild_opt_name = "";

    // 运行过程中显示窗口
    bool show_console = false;
    // 运行过程中隐藏窗口
    bool hide_console = false;

    // 抑制子进程输出
    bool suppress_output = false;

    // 启动参数传递
    std::string start_parameters = "";

    // 帮助
    bool help = false;

    // 原始参数信息
    int argc;
    char** argv;
};

app_args parse_app_args(int argc, char** argv);