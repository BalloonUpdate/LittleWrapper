#include "wrapper.h"
#include "iostream"
#include "fstream"
#include "utils.h"
#include "cJSON-1.7.14/cJSON.h"
#include "project.h"
#include "debug.h"
#include "vector"
#include "dir_utils.h"
#include "direct.h" // mkdir
#include "archive.h"
#include "magic.h"
#include "md5/md5.h"
#include "single_ins.h"

constexpr int split_block_len = 8;
constexpr uint8_t split_block[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

using namespace std;

// 检查出所有的文件夹的路径
// file_array：本地文件结构信息
// relativePath：当前相对路径
// 返回：所有目录的相对路径
static cJSON* get_dir_paths(cJSON* file_array, string relativePath = "")
{
    cJSON* result = cJSON_CreateArray();

    for (int i = 0; i < cJSON_GetArraySize(file_array); i++)
    {
        cJSON* item = cJSON_GetArrayItem(file_array, i);

        char* name = cJSON_GetObjectItem(item, "name")->valuestring;
        bool is_file = cJSON_IsTrue(cJSON_GetObjectItem(item, "is_file"));
        cJSON* children = cJSON_GetObjectItem(item, "children");

        if (!is_file)
        {
            cJSON* t = cJSON_CreateString((relativePath + name).c_str());
            cJSON_AddItemToArray(result, t);

            cJSON* r = get_dir_paths(children, relativePath + name + "/");
            for (int j = 0; j < cJSON_GetArraySize(r); j++)
                cJSON_AddItemToArray(result, cJSON_GetArrayItem(r, j));
        }
    }

    return result;
}

// 压缩文件夹
// file_array：本地文件结构信息
// source_dir：源目录的根路径
// temp_dir：临时目录，用来存放压缩好的数据
// relativePath：当前相对路径
// 返回所有压缩好的临时的.zlib文件的路径
static vector<string> compress_dir(cJSON* file_array, std::string source_dir, string temp_compressed_dir, string relativePath = "")
{
    vector<string> result;

    for (int i = 0; i < cJSON_GetArraySize(file_array); i++)
    {
        cJSON* item = cJSON_GetArrayItem(file_array, i);

        char* name = cJSON_GetObjectItem(item, "name")->valuestring;
        bool is_file = cJSON_IsTrue(cJSON_GetObjectItem(item, "is_file"));

        if (is_file)
        {
            string relative_file = relativePath + name;
            result.push_back(relative_file);

            printf("compressing: %s\n", relative_file.c_str());

            string source = source_dir + "\\" + relative_file;
            string dest = temp_compressed_dir + "\\" + get_string_md5(relative_file);
            deflate_file(source, dest);
        } else {
            cJSON* children = cJSON_GetObjectItem(item, "children");
            auto r = compress_dir(children, source_dir, temp_compressed_dir, relativePath + name + "\\");
            for (auto it = r.begin(); it != r.end(); ++it)
                result.push_back(*it);
        }
    }

    return result;
}

// 生成jumpdata
static string generate_jumpdata(uint64_t offset, uint64_t len)
{
    cJSON* json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "offset", offset);
    cJSON_AddNumberToObject(json, "len", len);

    char* printed = cJSON_PrintUnformatted(json);
    string result = printed;

    free(printed);
    cJSON_Delete(json);

    return result;
}

// 打包数据
// 返回pair<元数据的地址, 数据的长度>
static pair<uint32_t, uint32_t> pack_binaries2(fstream& fout, string source_dir, string temp_compressed_dir, optiondata& optdata)
{
    bool check_hash = optdata.check_hash;
    string exec = optdata.exec;
    auto metadata_addr = (uint32_t)fout.tellp();

    cJSON* file_array = dir_struct_to_json_in_list(generate_dir_struct(source_dir)); // 文件目录结构数据
    cJSON* metadata = cJSON_CreateObject();

    cJSON_AddBoolToObject(metadata, "check_hash", check_hash);
    cJSON_AddStringToObject(metadata, "exec", exec.c_str());
    cJSON_AddItemToObject(metadata, "directories", get_dir_paths(file_array));
    cJSON* address_table = cJSON_AddObjectToObject(metadata, "address_table");

    // 压缩文件
    auto t = compress_dir(file_array, source_dir, temp_compressed_dir);

    // 生成地址表
    uint32_t addr_offset = 0;
    for (int i = 0; i < t.size(); i++)
    {
        auto filename = t[i];
        auto hashed_filename = get_string_md5(filename);
        auto compressed_file = temp_compressed_dir + "\\" + hashed_filename;
        auto offset = addr_offset;
        auto len = get_file_length(compressed_file);
        auto hash = get_file_md5(compressed_file);
        auto raw_file = source_dir + "\\" + filename;
        auto raw_length = get_file_length(raw_file);
        auto raw_hash = get_file_md5(raw_file);

        cJSON* record = cJSON_AddObjectToObject(address_table, hashed_filename.c_str());
        cJSON_AddStringToObject(record, "raw_path", filename.c_str());
        cJSON_AddNumberToObject(record, "raw_size", raw_length);
        cJSON_AddStringToObject(record, "raw_hash", raw_hash.c_str());
        cJSON_AddNumberToObject(record, "offset", offset);
        cJSON_AddNumberToObject(record, "len", len);
        cJSON_AddStringToObject(record, "hash", hash.c_str());

        addr_offset += len + split_block_len;
    }

    // 1.写出整个metadata到文件
    char* metadata_text = cJSON_PrintUnformatted(metadata);
    int metadata_len = strlen(metadata_text);
    fout.write(metadata_text, strlen(metadata_text));
    fout.write((char*)split_block, split_block_len);
    free(metadata_text);
    cJSON_Delete(metadata);

    // 2.开始打包数据
    for (int i = 0; i < t.size(); i++)
    {
        auto filename = t[i];
        auto hashed_filename = get_string_md5(filename);
        auto compressed_file = temp_compressed_dir + "\\" + hashed_filename;
        std::fstream fin(compressed_file, std::fstream::in | std::fstream::binary);

        error_check(!fin.fail(), "write_binaries: could not open the target-file to extract: " + compressed_file);

        int buf_len = 16 * 1024;
        uint8_t* buf = new uint8_t[buf_len];

        uint32_t readBytes = 0;
        do {
            fin.read((char*)buf, buf_len);
            error_check(!fin.bad(), "write_binaries: could not read the target-file: " + compressed_file);
            readBytes = fin.gcount();

            fout.write((char*)buf, readBytes);
            error_check(!fout.bad(), "write_binaries: could not write the binary");
        } while (readBytes > 0);

        fout.write((char*)split_block, split_block_len);
        error_check(!fout.bad(), "write_binaries: could not write the splite data to the binary");
        fin.close();
    }

    return pair(metadata_addr, metadata_len);
}

// 打包文件夹
void lw_pack(string fileIn, string fileOut, string source_dir, string temp_compressed_dir, optiondata& optdata)
{
    // 创建文件夹
    string parent_dir = get_dir_name(fileOut);
    if (!file_exists(parent_dir))
        error_check(!_mkdir(parent_dir.c_str()), "pack_binaries: could not create the parent dir of the output-file: " + parent_dir);

    std::fstream fin(fileIn, std::fstream::in | std::fstream::binary);
    std::fstream fout(fileOut, std::fstream::out | std::fstream::binary | std::fstream::trunc);
    error_check(!fin.fail(), "pack_binaries: could not open the in-file: " + fileIn);
    error_check(!fout.fail(), "pack_binaries: could not open the out-file: " + fileOut);

    // 获取magic位置
    auto magic_offset = get_magic_offset(fin, (uint8_t*)MAGIC_HEADER, MAGIC_LEN);
    if (magic_offset == 0)
    {
        printf("Can not locate the magic-header in the executable file.\n");
        return;
    }
    printf("magic at: %llx\n", magic_offset);

    // 复制源文件
    int buf_size = 4 * 1024;
    uint8_t* buf = new uint8_t[buf_size];

    int readBytes = 0;
    do {
        fin.read((char*)buf, buf_size);
        error_check(!fin.bad(), "pack_binaries: could not copy the binary: read");
        readBytes = fin.gcount();
        fout.write((char*)buf, readBytes);
        error_check(!fout.bad(), "pack_binaries: could not copy the binary: write");
    } while (readBytes > 0);

    // 准备临时目录用来存放压缩后的数据
    if (!file_exists(temp_compressed_dir))
        error_check(!_mkdir(temp_compressed_dir.c_str()), "pack_binaries: could not create the temp dir: " + temp_compressed_dir);
    printf("tempdir: %s\n", temp_compressed_dir.c_str());

    // 写metadata
    printf("generate directory strcuture for %s\n", source_dir.c_str());
    auto metadata_pair = pack_binaries2(fout, source_dir, temp_compressed_dir, optdata);
    uint32_t metadata_addr = metadata_pair.first;
    uint32_t metatada_len = metadata_pair.second;

    // 写jumpdata
    uint64_t jumpdata_addr = magic_offset + MAGIC_LEN;
    string jumpdata = generate_jumpdata(metadata_addr, metatada_len);
    fout.seekp(jumpdata_addr);
    fout.write(jumpdata.c_str(), jumpdata.length());

    printf("wrote metadate at: %lx, magic at: %llx\n", metadata_addr, magic_offset);
    printf("wrote jumpdata data: 0x%llx, or %lld\n", jumpdata_addr, jumpdata_addr);

    fin.close();
    fout.close();
    delete[] buf;
}

// 解压数据
// 返回值： 1: 找不到jumpdata   2: 找不到metadata  3:数据损坏  4:无效jumpdata  5:无效metadata
int lw_extract(string fileIn, string extract_dir, bool single_ins_protection, optiondata* out_optdata)
{
    fstream fin(fileIn, fstream::in | fstream::binary);
    error_check(!fin.fail(), "extract_binaries: could not open the file to extract: " + fileIn);

    // 读取jumpdata
    uint64_t jumpdata_offset = get_magic_offset(fin, (uint8_t*)MAGIC_HEADER, MAGIC_LEN) + MAGIC_LEN;

    if (jumpdata_offset == 0)
    {
        printf("Can not locate the magic-header in the executable file.\n");
        return 1;
    }

    fin.seekg(jumpdata_offset);
    char* jumpdata = new char[PRESERVE_LEN - MAGIC_LEN];
    fin.read((char*)jumpdata, PRESERVE_LEN - MAGIC_LEN);
    error_check(!fin.bad(), "extract_binaries: could not read the jumpdata: " + fileIn);

    // 解析jumpdata
    cJSON* json = cJSON_Parse(jumpdata);
    if (json == nullptr)
    {
        return 4;
        delete[] jumpdata;
    }
    uint64_t metadata_addr = cJSON_GetObjectItem(json, "offset")->valueint;
    uint64_t metadata_len = cJSON_GetObjectItem(json, "len")->valueint;
    cJSON_Delete(json);
    delete[] jumpdata;

    if (metadata_addr == 0 || metadata_len == 0)
        return 2;

    // 读取metadata
    fin.clear();
    fin.seekg(metadata_addr);

    char* meta_buf = new char[metadata_len + 1];
    //memset(meta_buf, 0, metadata_len2);
    fin.read(meta_buf, metadata_len + 1);
    error_check(!fin.bad(), "extract_binaries: could not read the metadata: " + fileIn);

    printf("metadata offset: 0x%llx, len: %lld\n", metadata_addr, metadata_len);

    // 解析metadata
    cJSON* meta_json = cJSON_Parse(meta_buf);
    delete[] meta_buf;
    if (meta_json == nullptr)
        return 5;

    bool check_hash = cJSON_IsTrue(cJSON_GetObjectItem(meta_json, "check_hash"));
    string exec = cJSON_GetObjectItem(meta_json, "exec")->valuestring;
    cJSON* directories = cJSON_GetObjectItem(meta_json, "directories");
    cJSON* addr_table = cJSON_GetObjectItem(meta_json, "address_table");

    // 传出数据
    if (out_optdata != nullptr)
    {
        out_optdata->exec = exec;
        out_optdata->check_hash = check_hash;
    }

    // 单实例保护，当有多个实例存在时，后创建的实例不解压数据，直接运行就好，防止对先创建的运行中的实例造成文件破坏
    string write_protect_key = string("lw-sil-") + get_string_md5(extract_dir);
    printf("wtkey: %s\n", write_protect_key.c_str());
    bool write_protect = !request_single_instance_lock(write_protect_key);

    if (!write_protect)
    {
        // 建立解压输出目录
        string decompressed = extract_dir;
        if (!file_exists(decompressed))
            error_check(!_mkdir(decompressed.c_str()), "extract_binaries: could not create the extract-dir: " + decompressed);

        // 建立所有的文件夹(directories字段)
        for (int i = 0; i < cJSON_GetArraySize(directories); i++)
        {
            string dir = cJSON_GetArrayItem(directories, i)->valuestring;
            printf("mkdir: %s\n", dir.c_str());

            string cdir = decompressed + "\\" + string_replace(dir, "/", "\\");
            if (!file_exists(cdir))
                error_check(!_mkdir(cdir.c_str()), "extract_binaries: could not create the dir by the bounds: " + decompressed);
        }

        // 解压所有打包好的文件
        fin.clear();
        fin.seekg(metadata_addr);
        uint64_t base_addr = metadata_addr + metadata_len + split_block_len; // 末尾有8个是分隔符（都是0）

        printf("\nBaseOffset: 0x%llx\n", base_addr);
        printf("CheckHash: %s\n", check_hash?"check":"no_check");

        for (int i = 0; i < cJSON_GetArraySize(addr_table); i++)
        {
            cJSON* item = cJSON_GetArrayItem(addr_table, i);
            string key = item->string;
            string raw_path = cJSON_GetObjectItem(item, "raw_path")->valuestring;
            uint64_t raw_size = cJSON_GetObjectItem(item, "raw_size")->valueint;
            string raw_hash = cJSON_GetObjectItem(item, "raw_hash")->valuestring;
            uint64_t offset = cJSON_GetObjectItem(item, "offset")->valueint;
            uint64_t length = cJSON_GetObjectItem(item, "len")->valueint;
            string hash = cJSON_GetObjectItem(item, "hash")->valuestring;

            string target_file = decompressed + "\\" + string_replace(raw_path, "/", "\\");
            uint64_t addr = base_addr + offset;

            // 校验
            if (check_hash)
            {
                fin.clear();
                fin.seekg(addr);
                string md5 = get_stream_md5(fin, length);
                if (md5 != hash)
                {
                    printf("\nhash-check not passed, the file might be damaged!\nfile: %s\n", raw_path.c_str());
                    printf("hash-inside: %s\nhash-calculated: %s\n", hash.c_str(), md5.c_str());
                    printf("address: 0x%llx\nlength: %lld\n", addr, length);
                    return 3;
                }
            }

            // 如果文件大小和校验一样，则跳过解压，重复使用
            if (file_exists(target_file) && 
                get_file_length(target_file) == raw_size && 
                (!check_hash || get_file_md5(target_file) == raw_hash)
                )
            {
                printf("reuse: %s\n", raw_path.c_str());
                //int r = get_file_md5(target_file).compare(raw_hash);
                //printf("reuse: %s   -   hash: I:%s, O:%s, %d: %d\n", raw_path.c_str(), get_file_md5(target_file).c_str(), raw_hash.c_str(), r, check_hash);
                continue;
            }

            printf("decompress: %s, offset: 0x%llx, len: %lld\n", raw_path.c_str(), addr, length);
            // 解压
            inflate_to_file(fin, addr, length, target_file);
        }
    } else {
        printf("muilt instance is detected.\n");
    }

    // 释放资源
    fin.close();
    cJSON_Delete(meta_json);
    return 0;
}

// 详细信息
void lw_detail(string fileIn, string export_file)
{
    fstream fin(fileIn, fstream::in | fstream::binary);
    error_check(!fin.fail(), "detail_binaries: could not open the file to detail: " + fileIn);

    // 读取jumpdata
    uint64_t jumpdata_offset = get_magic_offset(fin, (uint8_t*)MAGIC_HEADER, MAGIC_LEN) + MAGIC_LEN;

    if (jumpdata_offset == 0)
    {
        printf("Can not locate the magic-header in the executable file.\n");
        return;
    }

    fin.seekg(jumpdata_offset);
    char* jumpdata = new char[PRESERVE_LEN - MAGIC_LEN];
    fin.read((char*)jumpdata, PRESERVE_LEN - MAGIC_LEN);
    error_check(!fin.bad(), "detail_binaries: could not read the jumpdata_offset: " + fileIn);

    printf("jumpdata at: 0x%llx\n%s\n", jumpdata_offset, jumpdata);

    // 解析jumpdata
    cJSON* json = cJSON_Parse(jumpdata);
    if (json == nullptr)
    {
        printf("the jumpdata is invaild\n");
        delete[] jumpdata;
        return;
    }
    uint64_t metadata_addr = cJSON_GetObjectItem(json, "offset")->valueint;
    uint64_t metadata_len = cJSON_GetObjectItem(json, "len")->valueint;
    cJSON_Delete(json);
    delete[] jumpdata;

    if (metadata_addr == 0 || metadata_len == 0)
    {
        printf("no detail could be shown.\nraw: off: %lld, len:%lld\n", metadata_addr, metadata_len);
        return;
    }

    // 读取元数据
    fin.clear();
    fin.seekg(metadata_addr);

    char* meta_buf = new char[metadata_len + 1];
    //memset(meta_buf, 0, metadata_len2);
    fin.read(meta_buf, metadata_len + 1);
    error_check(!fin.bad(), "detail_binaries: could not read the metadata: " + fileIn);

    printf("metadata offset: 0x%llx, len: %lld\n", metadata_addr, metadata_len);

    // 解析元数据
    cJSON* meta_json = cJSON_Parse(meta_buf);
    if (meta_json == nullptr)
    {
        printf("the metadata is invaild\n");
        delete[] meta_buf;
        return;
    }
    char* pretty = cJSON_Print(meta_json);

    if (export_file == "")
    {
        // 输出基本信息
        bool check_hash = cJSON_IsTrue(cJSON_GetObjectItem(meta_json, "check_hash"));
        string exec = cJSON_GetObjectItem(meta_json, "exec")->valuestring;
        printf("detail(check_hash: %s, exec: %s):\n----------\n%s\n----------\n", check_hash?"check":"no_check", exec.c_str(), pretty);

        // 计算基本偏移地址
        uint64_t base_addr = metadata_addr + metadata_len + split_block_len; // 末尾有8个是分隔符（都是0）
        printf("BaseAddr: 0x%llx\n", base_addr);

        // 输出所有目录
        cJSON* directories = cJSON_GetObjectItem(meta_json, "directories");
        for (int i = 0; i < cJSON_GetArraySize(directories); i++)
        {
            string path = cJSON_GetArrayItem(directories, i)->valuestring;

            printf("Directory: %s\n", path.c_str());
        }

        // 输出所有文件
        cJSON* addr_table = cJSON_GetObjectItem(meta_json, "address_table");
        for (int i = 0; i < cJSON_GetArraySize(addr_table); i++)
        {
            cJSON* item = cJSON_GetArrayItem(addr_table, i);
            string key = item->string;
            string path = cJSON_GetObjectItem(item, "raw_path")->valuestring;
            uint64_t offset = cJSON_GetObjectItem(item, "offset")->valueint;
            uint64_t length = cJSON_GetObjectItem(item, "len")->valueint;
            string hash = cJSON_GetObjectItem(item, "hash")->valuestring;
            uint64_t addr = base_addr + offset;

            printf("File: %s, offset: 0x%llx, len: %lld, %s\n", path.c_str(), addr, length, hash.c_str());
        }

        printf("----------\n");
    } else {
        std::fstream fout(export_file, std::fstream::out | std::fstream::trunc);
        error_check(!fout.fail(), "detail_binaries: could not open the export-file: " + export_file);
        fout.write(pretty, strlen(pretty));
        error_check(!fout.fail(), "detail_binaries: could not write to the export-file: " + export_file);
        fout.close();
        printf("detail wrote: %s\n", export_file.c_str());
    }

    fin.close();
    cJSON_Delete(meta_json);
    delete[] pretty;
    delete[] meta_buf;
}