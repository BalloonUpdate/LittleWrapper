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

// �������е��ļ��е�·��
// file_array�������ļ��ṹ��Ϣ
// relativePath����ǰ���·��
// ���أ�����Ŀ¼�����·��
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

// ѹ���ļ���
// file_array�������ļ��ṹ��Ϣ
// source_dir��ԴĿ¼�ĸ�·��
// temp_dir����ʱĿ¼���������ѹ���õ�����
// relativePath����ǰ���·��
// ��������ѹ���õ���ʱ��.zlib�ļ���·��
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

// ����jumpdata
static string generate_jumpdata(size_t offset, size_t len)
{
    cJSON* json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "offset", (double)offset);
    cJSON_AddNumberToObject(json, "len", (double)len);

    char* printed = cJSON_PrintUnformatted(json);
    string result = printed;

    free(printed);
    cJSON_Delete(json);

    return result;
}

// �������
// ����pair<Ԫ���ݵĵ�ַ, ���ݵĳ���>
static pair<size_t, size_t> pack_binaries2(fstream& fout, string source_dir, string temp_compressed_dir, optiondata& optdata)
{
    auto metadata_addr = fout.tellp();

    cJSON* file_array = dir_struct_to_json_in_list(generate_dir_struct(source_dir)); // �ļ�Ŀ¼�ṹ����
    cJSON* metadata = cJSON_CreateObject();

    cJSON_AddBoolToObject(metadata, "check_hash", optdata.check_hash);
    cJSON_AddStringToObject(metadata, "exec", optdata.exec.c_str());
    cJSON_AddBoolToObject(metadata, "show_console", optdata.show_console);

    cJSON_AddItemToObject(metadata, "directories", get_dir_paths(file_array));
    cJSON* address_table = cJSON_AddObjectToObject(metadata, "address_table");

    // ѹ���ļ�
    auto t = compress_dir(file_array, source_dir, temp_compressed_dir);

    // ���ɵ�ַ��
    size_t addr_offset = 0;
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
        cJSON_AddNumberToObject(record, "raw_size", (double)raw_length);
        cJSON_AddStringToObject(record, "raw_hash", raw_hash.c_str());
        cJSON_AddNumberToObject(record, "offset", (double)offset);
        cJSON_AddNumberToObject(record, "len", (double)len);
        cJSON_AddStringToObject(record, "hash", hash.c_str());

        addr_offset += len + split_block_len;
    }

    // 1.д������metadata���ļ�
    char* metadata_text = cJSON_PrintUnformatted(metadata);
    size_t metadata_len = strlen(metadata_text);
    fout.write(metadata_text, strlen(metadata_text));
    fout.write((char*)split_block, split_block_len);
    free(metadata_text);
    cJSON_Delete(metadata);

    // 2.��ʼ�������
    for (int i = 0; i < t.size(); i++)
    {
        auto filename = t[i];
        auto hashed_filename = get_string_md5(filename);
        auto compressed_file = temp_compressed_dir + "\\" + hashed_filename;
        std::fstream fin(compressed_file, std::fstream::in | std::fstream::binary);

        error_check(!fin.fail(), "write_binaries: could not open the target-file to extract: " + compressed_file);

        int buf_len = 16 * 1024;
        uint8_t* buf = new uint8_t[buf_len];

        streampos readBytes = 0;
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

// ��metadata���ȡoptiondata
optiondata get_optiondata(cJSON* metadata)
{
    bool check_hash = cJSON_IsTrue(cJSON_GetObjectItem(metadata, "check_hash"));
    string exec = cJSON_GetObjectItem(metadata, "exec")->valuestring;
    bool show_console = cJSON_IsTrue(cJSON_GetObjectItem(metadata, "show_console"));

    optiondata od;
    od.exec = exec;
    od.check_hash = check_hash;
    od.show_console = show_console;

    return od;
}

// ��ȡmetadata
// fileIn�����Ǹ��ļ���ȡ
// out_addr��metadata���Ǹ�λ��
// out_len��metadata�ĳ���
// out_metadata��metadata���
void lw_read_metadata(string fileIn, cJSON** out_metadata, size_t* out_addr, size_t* out_len)
{
    fstream fin(fileIn, fstream::in | fstream::binary);
    error_check(!fin.fail(), "could not open the file to extract: " + fileIn);

    // ��ȡjumpdata
    size_t jumpdata_offset = get_magic_offset(fin, (uint8_t*)MAGIC_HEADER, MAGIC_LEN) + MAGIC_LEN;

    if (jumpdata_offset == 0)
        throw jumpdata_not_found_exception();

    fin.seekg(jumpdata_offset);
    char* jumpdata = new char[PRESERVE_LEN - MAGIC_LEN];
    fin.read((char*)jumpdata, PRESERVE_LEN - MAGIC_LEN);
    error_check(!fin.bad(), "could not read the jumpdata: " + fileIn);

    // ����jumpdata
    cJSON* json = cJSON_Parse(jumpdata);
    if (json == nullptr)
    {
        delete[] jumpdata;
        throw jumpdata_invaild_exception();
    }
    size_t metadata_addr = cJSON_GetObjectItem(json, "offset")->valueint;
    size_t metadata_len = cJSON_GetObjectItem(json, "len")->valueint;
    cJSON_Delete(json);
    delete[] jumpdata;

    if (metadata_addr == 0 || metadata_len == 0)
        throw metadata_not_found_exception();

    // ��ȡmetadata
    fin.clear();
    fin.seekg(metadata_addr);

    char* meta_buf = new char[metadata_len + 1];
    //memset(meta_buf, 0, metadata_len2);
    fin.read(meta_buf, metadata_len + 1);
    error_check(!fin.bad(), "could not read the metadata: " + fileIn);

    printf("metadata offset: 0x%llx, len: %lld\n", metadata_addr, metadata_len);

    // ����metadata
    cJSON* meta_json = cJSON_Parse(meta_buf);
    delete[] meta_buf;
    if (meta_json == nullptr)
        throw metadata_invaild_exception();;

    // ��������
    if (out_metadata != nullptr)
        *out_metadata = meta_json;
    else
        cJSON_Delete(meta_json);

    if (out_addr)
        *out_addr = metadata_addr;

    if(out_len)
        *out_len = metadata_len;

    // �ͷ���Դ
    fin.close();
}

// ����ļ���
void lw_pack(string fileIn, string fileOut, string source_dir, string temp_compressed_dir, optiondata& optdata)
{
    // �����ļ���
    string parent_dir = get_dir_name(fileOut);
    if (!file_exists(parent_dir))
        error_check(!_mkdir(parent_dir.c_str()), "pack_binaries: could not create the parent dir of the output-file: " + parent_dir);

    std::fstream fin(fileIn, std::fstream::in | std::fstream::binary);
    std::fstream fout(fileOut, std::fstream::out | std::fstream::binary | std::fstream::trunc);
    error_check(!fin.fail(), "pack_binaries: could not open the in-file: " + fileIn);
    error_check(!fout.fail(), "pack_binaries: could not open the out-file: " + fileOut);

    // ��ȡmagicλ��
    auto magic_offset = get_magic_offset(fin, (uint8_t*)MAGIC_HEADER, MAGIC_LEN);
    if (magic_offset == 0)
    {
        printf("Can not locate the magic-header in the executable file.\n");
        return;
    }
    printf("magic at: %llx\n", magic_offset);

    // ����Դ�ļ�
    int buf_size = 4 * 1024;
    uint8_t* buf = new uint8_t[buf_size];

    streamsize readBytes = 0;
    do {
        fin.read((char*)buf, buf_size);
        error_check(!fin.bad(), "pack_binaries: could not copy the binary: read");
        readBytes = fin.gcount();
        fout.write((char*)buf, readBytes);
        error_check(!fout.bad(), "pack_binaries: could not copy the binary: write");
    } while (readBytes > 0);

    // ׼����ʱĿ¼�������ѹ���������
    if (!file_exists(temp_compressed_dir))
        error_check(!_mkdir(temp_compressed_dir.c_str()), "pack_binaries: could not create the temp dir: " + temp_compressed_dir);
    printf("tempdir: %s\n", temp_compressed_dir.c_str());

    // дmetadata
    printf("generate directory strcuture for %s\n", source_dir.c_str());
    auto metadata_pair = pack_binaries2(fout, source_dir, temp_compressed_dir, optdata);
    size_t metadata_addr = metadata_pair.first;
    size_t metatada_len = metadata_pair.second;

    // дjumpdata
    uint64_t jumpdata_addr = magic_offset + MAGIC_LEN;
    string jumpdata = generate_jumpdata(metadata_addr, metatada_len);
    fout.seekp(jumpdata_addr);
    fout.write(jumpdata.c_str(), jumpdata.length());

    printf("wrote metadate at: %llx, magic at: %llx\n", metadata_addr, magic_offset);
    printf("wrote jumpdata data: 0x%llx, or %lld\n", jumpdata_addr, jumpdata_addr);

    fin.close();
    fout.close();
    delete[] buf;
}

// ��ѹ����
void lw_extract(string fileIn, string extract_dir, bool single_ins_protection)
{
    // ��ȡmetadata
    size_t metadata_addr;
    size_t metadata_len;
    cJSON* meta;

    lw_read_metadata(fileIn, &meta, &metadata_addr, &metadata_len);

    optiondata opt = get_optiondata(meta);
    cJSON* directories = cJSON_GetObjectItem(meta, "directories");
    cJSON* addr_table = cJSON_GetObjectItem(meta, "address_table");

    // ��ʵ�����������ж��ʵ������ʱ���󴴽���ʵ������ѹ���ݣ�ֱ�����оͺã���ֹ���ȴ����������е�ʵ������ļ��ƻ�
    string write_protect_key = string("lw-sil-") + get_string_md5(extract_dir);
    printf("wtkey: %s\n", write_protect_key.c_str());
    bool write_protect = !request_single_instance_lock(write_protect_key);

    if (!write_protect)
    {
        // ׼����ѹ����
        fstream fin(fileIn, fstream::in | fstream::binary);
        error_check(!fin.fail(), "extract_binaries: could not open the file to extract: " + fileIn);

        // ������ѹ���Ŀ¼
        string decompressed = extract_dir;
        if (!file_exists(decompressed))
            error_check(!_mkdir(decompressed.c_str()), "extract_binaries: could not create the extract-dir: " + decompressed);

        // �������е��ļ���(directories�ֶ�)
        for (int i = 0; i < cJSON_GetArraySize(directories); i++)
        {
            string dir = cJSON_GetArrayItem(directories, i)->valuestring;
            printf("mkdir: %s\n", dir.c_str());

            string cdir = decompressed + "\\" + string_replace(dir, "/", "\\");
            if (!file_exists(cdir))
                error_check(!_mkdir(cdir.c_str()), "extract_binaries: could not create the dir by the bounds: " + decompressed);
        }

        // ��ѹ���д���õ��ļ�
        fin.clear();
        fin.seekg(metadata_addr);
        size_t base_addr = metadata_addr + metadata_len + split_block_len; // ĩβ��8���Ƿָ���������0��

        printf("\nBaseOffset: 0x%llx\n", base_addr);
        printf("CheckHash: %s\n", opt.check_hash?"check":"no_check");

        for (int i = 0; i < cJSON_GetArraySize(addr_table); i++)
        {
            cJSON* item = cJSON_GetArrayItem(addr_table, i);
            string key = item->string;
            string raw_path = cJSON_GetObjectItem(item, "raw_path")->valuestring;
            size_t raw_size = cJSON_GetObjectItem(item, "raw_size")->valueint;
            string raw_hash = cJSON_GetObjectItem(item, "raw_hash")->valuestring;
            size_t offset = cJSON_GetObjectItem(item, "offset")->valueint;
            size_t length = cJSON_GetObjectItem(item, "len")->valueint;
            string hash = cJSON_GetObjectItem(item, "hash")->valuestring;

            string target_file = decompressed + "\\" + string_replace(raw_path, "/", "\\");
            size_t addr = base_addr + offset;

            // У��
            if (opt.check_hash)
            {
                fin.clear();
                fin.seekg(addr);
                string md5 = get_stream_md5(fin, length);
                if (md5 != hash)
                {
                    printf("\nhash-check not passed, the file might be damaged!\nfile: %s\n", raw_path.c_str());
                    printf("hash-inside: %s\nhash-calculated: %s\n", hash.c_str(), md5.c_str());
                    printf("address: 0x%llx\nlength: %lld\n", addr, length);
                    throw binaries_damaged_exception();
                }
            }

            // ����ļ���С��У��һ������������ѹ���ظ�ʹ��
            if (file_exists(target_file) && get_file_length(target_file) == raw_size && (!opt.check_hash || get_file_md5(target_file) == raw_hash))
            {
                printf("reuse: %s\n", raw_path.c_str());
                continue;
            }

            printf("decompress: %s, offset: 0x%llx, len: %lld\n", raw_path.c_str(), addr, length);
            // ��ѹ
            inflate_to_file(fin, addr, length, target_file);
        }

        fin.close();
    } else {
        printf("muilt instance is detected.\n");
    }

    // �ͷ���Դ
    cJSON_Delete(meta);
}

// ��ϸ��Ϣ
void lw_detail(string fileIn, string export_file)
{
    // ��ȡmetadata
    size_t metadata_addr;
    size_t metadata_len;
    cJSON* meta;
    lw_read_metadata(fileIn, &meta, &metadata_addr, &metadata_len);

    // ��ӡmetadata
    char* pretty = cJSON_Print(meta);

    if (export_file == "")
    {
        // ���������Ϣ
        optiondata od = get_optiondata(meta);
        printf("detail:\n  check_hash: %d\n  exec: %s\n  show_console: %d\n----------\n%s\n----------\n", od.check_hash, od.exec.c_str(), od.show_console, pretty);

        // �������ƫ�Ƶ�ַ
        size_t base_addr = metadata_addr + metadata_len + split_block_len; // ĩβ��8���Ƿָ���������0��
        printf("BaseAddr: 0x%llx\n", base_addr);

        // �������Ŀ¼
        cJSON* directories = cJSON_GetObjectItem(meta, "directories");
        for (int i = 0; i < cJSON_GetArraySize(directories); i++)
        {
            string path = cJSON_GetArrayItem(directories, i)->valuestring;
            printf("Directory: %s\n", path.c_str());
        }

        // ��������ļ�
        cJSON* addr_table = cJSON_GetObjectItem(meta, "address_table");
        for (int i = 0; i < cJSON_GetArraySize(addr_table); i++)
        {
            cJSON* item = cJSON_GetArrayItem(addr_table, i);
            string key = item->string;
            string path = cJSON_GetObjectItem(item, "raw_path")->valuestring;
            size_t offset = cJSON_GetObjectItem(item, "offset")->valueint;
            size_t length = cJSON_GetObjectItem(item, "len")->valueint;
            string hash = cJSON_GetObjectItem(item, "hash")->valuestring;
            size_t addr = base_addr + offset;

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

    cJSON_Delete(meta);
    delete[] pretty;
}