﻿#include <iostream>
#include <stdio.h>
#include "utils.h"
#include "env.h"
#include "cJSON-1.7.14/cJSON.h"
#include "archive.h"
#include "dir_utils.h"
#include "wrapper.h"
#include "runner.h"
#include "project.h"
#include "args_utils.h"

// 程序读取自身而不是-attach.exe文件
#define RELEASE_MODE

using namespace std;

uint8_t preserveSection[PRESERVE_LEN] = MAGIC_HEADER "{\"offset\":0, \"len\":0}";

void output_help()
{
    printf("Sub commands available:\n");
    printf("  --help\n");
    printf("  --pack <source_dir> --command <exec> [--output <output_file>] [--no-hashing]\n");
    printf("  --extract [--output <output_dir>]\n");
    printf("  --detail\n");
}

int functions(app_arguments args, string workdir, string executable)
{
    if (args.help)
    {
        output_help();
    } else if (args.optarg_required) {
        printf("require option for arg: %s\n", args.invaild_opt_name.c_str());
        output_help();
    } else if (args.unknown_opt) {
        printf("invaild option: %s\n", args.invaild_opt_name.c_str());
        output_help();
    } else if (args.pack) {
        // 数据目录
        string source_dir = args.pack_src;
        if (is_relative_path(source_dir))
        {
            bool starts_with_slash = string_starts_with(string_replace(source_dir, "/", "\\"), "\\");
            source_dir = workdir + (starts_with_slash ? "" : "\\") + source_dir;
        }

        // 文件检查
        if (!file_exists(source_dir) || !is_file_a_dir(source_dir))
        {
            printf("the source_dir could not be found or was not a directory: %s\n", source_dir.c_str());
            return 1;
        }

        // 命令检查
        if (args.pack_exec == "") 
        {
            printf("the option \"exec\" was required\n");
            return 1;
        }

        printf("<<< %s\n", args.pack_exec.c_str());

        printf("pack with source_dir: %s\n", source_dir.c_str());
        string output_file = args.output != "" ? (is_relative_path(args.output)? workdir + "\\" : "") + args.output : workdir + "\\" + get_filename(source_dir) + ".exe";
        string temp_dir = "temp-compressed";
        printf("output: %s\n", output_file.c_str());

        pack_binaries(executable, output_file, source_dir, temp_dir, !args.pack_no_hash, args.pack_exec);

        // 清理临时文件
        if (file_exists(temp_dir))
            remove_dir(temp_dir);

        printf("\nfinish\n");
    } else if (args.extract) {
        printf("extract\n");
        string output_dir = args.output != "" ? (is_relative_path(args.output) ? workdir + "\\" : "") + args.output : workdir + "\\" + get_filename(executable);

        // 清理临时文件
        if (file_exists(output_dir))
            remove_dir(output_dir);

        switch (extract_binaries(executable, output_dir))
        {
        case 1:
            show_dialog(PROJ_VER, "程序损坏，无法读取标识数据(MagicHeader)");
            return 1;
        case 2:
            show_dialog(PROJ_VER, "应用程序内没有任何打包数据");
            return 1;
        case 3:
            show_dialog(PROJ_VER, "程序损坏，无法读取对应的数据");
            return 1;
        case 4:
            show_dialog(PROJ_VER, "程序损坏，无法读取对应的数据(Jumpdata)");
            return 1;
        case 5:
            show_dialog(PROJ_VER, "程序损坏，无法读取对应的数据(Metadata)");
            return 1;
        }
    } else if (args.detail) {
        printf("detail\n");
        string source = executable;
        detail_binaries(source);
    } else {
        printf("invaild options\n");
        output_help();
    }

    return 0;
}

int main(int argc, char** argv)
{
    printf("%s\n\n", PROJ_VER);
    if(argc > 999999999999999999)
        printf("preserveSection: %s\n\n", (char*)preserveSection + MAGIC_LEN);

    string executable = get_exe_path();
    string workdir = get_current_work_dir();

    if (argc == 1)
    {
        string source = executable;
        string temp_dir = get_temp_directory() + string("-") + get_string_md5(executable);

        printf("execute\n");
        return run_program(source, temp_dir);
    } else {
        functions(parse_args(argc, argv), workdir, executable);
    }
}
