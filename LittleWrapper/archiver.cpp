#include "archiver.h"
#include "debug.h"
#include "magic.h"
#include <string>
#include <fstream>
#include "exceptions/exceptions.h"
#include "json_obj.h"
#include "utils/general_utils.h"
#include "utils/env_utils.h"
#include "utils/dir_utils.h"
#include "archive.h"
#include "single_ins.h"
#include <direct.h>
#include "pe_resource.h"

using namespace std;

const int split_block_len = 8;
const uint8_t split_block[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

/// <summary>
/// �������е��ļ��е�·��
/// </summary>
/// <param name="file_array">�����ļ��ṹ��Ϣ</param>
/// <param name="relativePath">��ǰ���·��</param>
/// <returns>����Ŀ¼�����·��</returns>
static vector<string> get_all_dirs(vector<file_struct> file_array, string relativePath = "")
{
    vector<string> result;

    for (size_t i = 0; i < file_array.size(); i++)
    {
        file_struct file = file_array[i];

        if (!file.is_file)
        {
            result.emplace_back(relativePath + file.name);

            vector<string> r = get_all_dirs(file.children, relativePath + file.name + "/");
            for (int j = 0; j < r.size(); j++)
                result.emplace_back(r[j]);
        }
    }

    return result;
}

/// <summary>
/// vector{string}תjson_obj�б�
/// </summary>
/// <param name="list"></param>
/// <returns></returns>
static json_obj vector2json(vector<string> list)
{
    json_obj result = json_obj::create_array();

    for (int i = 0; i < list.size(); i++)
        result.add_item(list[i]);

    return result;
}

/// <summary>
/// �����ļ��нṹ
/// </summary>
/// <param name="path">Ҫ���ɵ�·��</param>
/// <returns>�ļ��нṹ��Ϣ</returns>
static json_obj files2json(vector<file_struct> files)
{
    json_obj list = json_obj::create_array();

    for (int i = 0; i < files.size(); i++)
    {
        file_struct file = files[i];
        json_obj entry = json_obj::create_object();
        entry.set_object("is_file", file.is_file);
        entry.set_object("name", file.name);
        entry.set_object("length", (int) file.length);
        entry.set_object("children", files2json(file.children));
        list.add_item(entry);
    }

    return list;
}

/// <summary>
/// ִ���ļ��д������
/// </summary>
/// <param name="files">�ļ��ṹ��Ϣ</param>
/// <param name="record_list">�����ص��ļ�����д������</param>
/// <param name="source_dir">ԴĿ¼�ĸ�·��</param>
/// <param name="temp_dir">��ʱĿ¼���������ѹ���õ�����</param>
/// <param name="relative_path">��ǰ���·��</param>
static void pack_with_dir(vector<file_struct> files, json_obj& record_list, std::string source_dir, string temp_dir, string relative_path = "")
{
    for (int i = 0; i < files.size(); i++)
    {
        file_struct file = files[i];
        string relative_file = relative_path + file.name;

        if (file.is_file)
        {
            string source_file = source_dir + "\\" + relative_file;
            string hash = get_file_md5(source_file);
            string compressed = temp_dir + "\\" + hash;

            // ����ָʾ
            int last_percent = 0;
            int remains_count = 0;
            archive_on_processing cb = [&last_percent, &remains_count](size_t processed, size_t total) {
                size_t percent = processed * 100 / total;
                if (percent != last_percent)
                {
                    //printf(" (%d -> %d) ", last_percent, percent);

                    int delta = percent - last_percent;
                    for (int i = 0; i < delta / 10; i++)
                        printf(".");

                    remains_count += delta % 10;
                    if (remains_count >= 10)
                    {
                        for (int i = 0; i < remains_count / 10; i++)
                            printf(".");
                        remains_count %= 10;
                    }

                    last_percent = percent;
                }
            };

            // ִ��ѹ��
            printf("compressing: %s ", relative_file.c_str());
            deflate_file(source_file, compressed, cb);
            printf("\n");

            // ����ļ�����
            json_obj entry = json_obj::create_object();
            entry.set_object("raw_path", relative_file);
            entry.set_object("raw_len", (int) file.length);
            entry.set_object("raw_hash", hash);
            entry.set_object("len", (int) get_file_length(compressed));
            entry.set_object("hash", get_file_md5(compressed));
            record_list.add_item(entry);
        } else {
            pack_with_dir(file.children, record_list, source_dir, temp_dir, relative_file + "\\");
        }
    }
}

/// <summary>
/// ��������ļ���Ŀ¼
/// </summary>
/// <param name="base_path">Ҫ�����Ŀ¼·��</param>
/// <param name="relative_path">���Ŀ¼</param>
/// <param name="local_files">Ҫ�����Աȵı����ļ�</param>
/// <param name="template_files">��Ҫ������ļ�</param>
/// <param name="template_directories">��Ҫ�����Ŀ¼</param>
static void clean_folder(string base_path, string relative_path, vector<file_struct> local_files, vector<string>& template_files, vector<string>& template_directories)
{
    for (int i = 0; i < local_files.size(); i++)
    {
        file_struct entry = local_files[i];
        string relative_entry = relative_path + entry.name;

        if (entry.is_file)
        {
            bool found = false;
            for (int i = 0; i < template_files.size(); i++)
            {
                if (relative_entry == template_files[i])
                {
                    found = true;
                    break;
                }
            }

            if (!found)
                remove_file_or_dir(base_path + "\\" + relative_entry);
        } else {
            clean_folder(
                base_path,
                relative_path + (relative_path.empty() ? "" : "\\") + entry.name,
                entry.children,
                template_files, 
                template_directories
            );

            bool found = false;
            for (int i = 0; i < template_directories.size(); i++)
            {
                if (template_directories[i] == relative_entry)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
                remove_file_or_dir(base_path + "\\" + relative_entry);
        }
    }
}

archiver::archiver(std::string file, ios_base::openmode rw_mode)
{
    this->file = file;

    fstream* fs = new fstream(file, rw_mode);
    stream.reset(fs, [] (fstream* p) { p->close(); });
    error_check(!stream->fail(), "could not open the file: " + file);
}

archiver::~archiver()
{
    
}

archiver::archiver(archiver& other)
{
    file = other.file;
    stream = other.stream;
}

archiver::archiver(archiver&& other)
{
    file = other.file;
    stream = other.stream;
}

archiver& archiver::operator=(archiver& other)
{
    file = other.file;
    stream = other.stream;
    return *this;
}

archiver& archiver::operator=(archiver&& other)
{
    file = other.file;
    stream = other.stream;
    return *this;
}

void archiver::lw_pack(std::string dest_file, std::string source_dir, std::string temp_dir, lw_options& options)
{
    string source_file = get_exe_path();

    // �����ļ���
    string parent_dir = get_dir_name(dest_file);
    if (!file_exists(parent_dir))
        error_check(!_mkdir(parent_dir.c_str()), "failed to create the parent dir of the output-file: " + parent_dir);

    // ׼����ʱĿ¼�������ѹ���������
    if (!file_exists(temp_dir))
        error_check(!_mkdir(temp_dir.c_str()), "failed to create the temp dir: " + temp_dir);
    else
        clear_dir(temp_dir);

    // 0.��ȡĿ¼�ṹ
    vector<file_struct> strcut = cal_dir_struct(source_dir);

    if (strcut.size() == 0)
        throw failed_to_pack_exception();

    // 1.����EXE�ļ�ģ��
    std::fstream fs_source(source_file, std::fstream::in | std::fstream::binary);
    error_check(!fs_source.fail(), "failed to open the source-file: " + source_file);
    std::fstream fs_dest(dest_file, std::fstream::out | std::fstream::binary | std::fstream::trunc);
    error_check(!fs_dest.fail(), "failed to open the dest-file: " + dest_file);
    {
        int buf_size = 64 * 1024;
        uint8_t* buf = new uint8_t[buf_size];
        streamsize readBytes = 0;
        do  {
            fs_source.read((char*) buf, buf_size);
            error_check(!fs_source.bad(), "failed to copy the binary: read");
            readBytes = fs_source.gcount();
            fs_dest.write((char*) buf, readBytes);
            error_check(!fs_dest.bad(), "failed to copy the binary: write");
        } while (readBytes > 0);
        delete[] buf;
    }
    fs_source.close();
    fs_dest.close();

    // 2.д��PeResource����
    // һ.ѡ������
    json_obj optiondata = json_obj::create_object();
    optiondata.set_object("check_hash", options.check_hash);
    optiondata.set_object("exec", options.exec);
    optiondata.set_object("show_console", options.show_console);

    // һ.Ŀ¼����
    json_obj directories = vector2json(get_all_dirs(strcut));

    // ��.�ļ�����
    json_obj filetable = json_obj::create_array();
    // ѹ���ļ�
    pack_with_dir(strcut, filetable, source_dir, temp_dir);

    // ���µ�ַ�����offset���ԣ�Ȼ��
    size_t addr_offset = 0;
    for (int i = 0; i < filetable.get_array_size(); i++)
    {
        filetable[i].set_object("relative_offset", (int) addr_offset);
        addr_offset += filetable[i].get_object_int("len") + split_block_len;
    }

    // д��Ԫ����
    string text_optiondata = optiondata.to_string(true);
    string text_directories = directories.to_string(true);
    string text_filetable = filetable.to_string(true);

    pe_resource_writer res_writer(dest_file);
    res_writer.update_resouce("little_wrapper", "options", (LPVOID) text_optiondata.c_str(), text_optiondata.length() + 1);
    res_writer.update_resouce("little_wrapper", "directories", (LPVOID) text_directories.c_str(), text_directories.length() + 1);
    res_writer.update_resouce("little_wrapper", "filetable", (LPVOID) text_filetable.c_str(), text_filetable.length() + 1);
    res_writer.close();

    // 3.д��������ƫ������
    size_t dest_file_size = get_file_length(dest_file);

    std::fstream fs_dest2(dest_file, std::fstream::in | std::fstream::out | std::fstream::binary);
    error_check(!fs_dest2.fail(), "failed to open the dest-file: " + dest_file);

    // д������ƫ������
    size_t addr = get_preserved_data_address(fs_dest2, true);
    fs_dest2.seekg(addr);

    json_obj offset_data = json_obj::create_object();
    offset_data.set_object("offset", (int) dest_file_size);
    string offset_data_text = offset_data.to_string(false);

    fs_dest2.write((char*) offset_data_text.c_str(), offset_data_text.length() + 1);
    error_check(!fs_dest2.bad(), "failed to write offset-data to pe-file for binaries when packing: " + dest_file);

    fs_dest2.seekg(0, ios_base::end);

    // 4.д������������
    for (int i = 0; i < filetable.get_array_size(); i++)
    {
        json_obj entry = filetable[i];
        string compressed_file = temp_dir + "\\" + entry.get_object_string("raw_hash");

        std::fstream fin(compressed_file, std::fstream::in | std::fstream::binary);
        error_check(!fin.fail(), "failed to open the binary-file when packing: " + compressed_file);

        int buf_len = 64 * 1024;
        uint8_t* buf = new uint8_t[buf_len];
        streampos bytes_read = 0;
        do {
            fin.read((char*)buf, buf_len);
            error_check(!fin.bad(), "failed to read the binary-file when packing: " + compressed_file);
            bytes_read = fin.gcount();

            fs_dest2.write((char*) buf, bytes_read);
            error_check(!fs_dest2.bad(), "failed to write to pe-file when packing: " + dest_file);
        } while (bytes_read > 0);
        fs_dest2.write((char*) split_block, split_block_len);
        error_check(!fs_dest2.bad(), "failed to write the splite data to pe-file when packing: " + dest_file);

        fin.close();
    }
    fs_dest2.close();
}

void archiver::lw_extract(std::string file_to_extract, std::string extract_dir, bool single_ins_protection, bool no_output)
{
    json_obj file_table = archiver::read_file_table(file_to_extract);
    archiver::lw_options options = archiver::read_options(file_to_extract);
    vector<string> directories = archiver::read_directories(file_to_extract);

    // ��ʵ�����������ж��ʵ������ʱ���󴴽���ʵ������ѹ���ݣ�ֱ�����оͺã���ֹ���ȴ����������е�ʵ������ļ��ƻ�
    string write_protect_key = string("lw-sil-") + get_string_md5(extract_dir);
    printf("write protection key: %s\n", write_protect_key.c_str());
    bool no_second_ins = request_single_instance_lock(write_protect_key);

    if (no_second_ins)
    {
        // ������ѹ���Ŀ¼
        if (!file_exists(extract_dir))
            error_check(!_mkdir(extract_dir.c_str()), "failed to create the dir to extract: " + extract_dir);

        // ���������ļ�
        vector<string> _files; // ��Ҫ������ļ���·��
        for (int i = 0; i < file_table.get_array_size(); i++)
            _files.emplace_back(string_replace(file_table[i].get_object_string("raw_path"), "/", "\\"));
        vector<string> _dirs; // ��Ҫ�����Ŀ¼��·��
        for (int i = 0; i < directories.size(); i++)
            _files.emplace_back(string_replace(directories[i], "/", "\\"));
        vector<file_struct> local_files = cal_dir_struct(extract_dir); // ���������ļ�״��

        clean_folder(extract_dir, "", local_files, _files, _files);

        // �������е��ļ���(directories�ֶ�)
        for (int i = 0; i < directories.size(); i++)
        {
            string dir = directories[i];

            string cdir = extract_dir + "\\" + string_replace(dir, "/", "\\");
            if (!file_exists(cdir))
            {
                if (!no_output)
                    printf("mkdir: %s\n", dir.c_str());

                error_check(!_mkdir(cdir.c_str()), "failed to create the dir for binaries: " + cdir);
            }
        }

        // ��ȡ����������ƫ��
        json_obj pdata(string(get_preserved_data(), get_preserved_data_len()));
        size_t base_offset = pdata.get_object_int("offset");

        // ��ѹ���д���õ��ļ�
        printf("\nbase_offset: 0x%llx      CheckHash: %s\n", base_offset, options.check_hash ? "check" : "no_check");

        std::fstream fs(file_to_extract, std::fstream::in | std::fstream::binary);
        error_check(!fs.fail(), "failed to open the file: " + file_to_extract);

        for (int i = 0; i < file_table.get_array_size(); i++)
        {
            json_obj item = file_table[i];

            string raw_path = item.get_object_string("raw_path");
            size_t raw_len = item.get_object_int("raw_len");
            string raw_hash = item.get_object_string("raw_hash");
            size_t relative_offset = item.get_object_int("relative_offset");
            size_t length = item.get_object_int("len");
            string hash = item.get_object_string("hash");

            string target_file = extract_dir + "\\" + string_replace(raw_path, "/", "\\");
            size_t bin_addr = base_offset + relative_offset;

            // У��
            if (options.check_hash)
            {
                fs.clear();
                fs.seekg(bin_addr);
                string md5 = get_stream_md5(fs, length);
                if (md5 != hash)
                {
                    printf("\nhash-check not passed, the file might be damaged!\nfile: %s\n", raw_path.c_str());
                    printf("hash-inside: %s\nhash-calculated: %s\n", hash.c_str(), md5.c_str());
                    printf("address: 0x%llx\nlength: %lld\n", bin_addr, length);
                    throw binaries_damaged_exception();
                }
            }

            // ����ļ���С��У��һ������������ѹ���ظ�ʹ��
            if (file_exists(target_file) && 
                get_file_length(target_file) == raw_len &&  
                (!options.check_hash || get_file_md5(target_file) == raw_hash)
            ) {
                if (!no_output)
                    printf("reuse: %s\n", raw_path.c_str());
                continue;
            }

            if (!no_output)
                printf("decompress: %s, offset: 0x%llx, len: %lld, raw_len: %lld\n", raw_path.c_str(), bin_addr, length, raw_len);

            // ��ѹ
            inflate_to_file(fs, bin_addr, length, target_file);
        }

        fs.close();
    } else  {
        printf("muilt instance is detected.\n");
    }
}

void archiver::lw_detail(std::string file_to_read)
{
    json_obj file_table = archiver::read_file_table(file_to_read);
    archiver::lw_options options = archiver::read_options(file_to_read);
    vector<string> directories = archiver::read_directories(file_to_read);

    // ���options
    printf("---------------options----------------\n");
    printf("options:\n  check_hash: %d\n  exec: %s\n  show_console: %d\n",
        options.check_hash,
        options.exec.c_str(),
        options.show_console
    );

    // ���������Ϣ
    printf("---------------directories----------------\n");
    for (int i = 0; i < directories.size(); i++)
        printf("Directory: %s\n", directories[i].c_str());

    // �������ƫ�Ƶ�ַ
    printf("---------------file-table----------------\n");
    std::fstream fs(get_exe_path(), std::fstream::in | std::fstream::binary);
    error_check(!fs.fail(), "failed to open the file: " + get_exe_path());
    size_t base_address = get_preserved_data_address(fs, true);
    fs.close();
    printf("base_address: 0x%llx\n", base_address);

    for (int i = 0; i < file_table.get_array_size(); i++)
    {
        json_obj item = file_table[i];
        string path = item.get_object_string("raw_path");
        size_t relative_offset = item.get_object_int("relative_offset");
        size_t length = item.get_object_int("len");
        string hash = item.get_object_string("hash");
        size_t addr = base_address + relative_offset;

        printf("File: %s, file_addr: 0x%llx, len: %lld, %s\n", path.c_str(), addr, length, hash.c_str());
    }

    printf("----------\n");
}

archiver::lw_options archiver::read_options(std::string file_to_read)
{
    pe_resource_reader res_reader(file_to_read);
    auto res = res_reader.open_resource("little_wrapper", "options");
    json_obj options_json(string((char*) res.data, res.size));
    res_reader.close();

    lw_options options;
    options.check_hash = options_json.get_object_bool("check_hash");
    options.exec = options_json.get_object_string("exec");
    options.show_console = options_json.get_object_bool("show_console");

    return options;
}

vector<string> archiver::read_directories(std::string file_to_read)
{
    pe_resource_reader res_reader(file_to_read);
    auto res = res_reader.open_resource("little_wrapper", "directories");
    json_obj directories_json(string((char*) res.data, res.size));
    res_reader.close();
    vector<string> directories;

    for (int i = 0; i < directories_json.get_array_size(); i++)
        directories.emplace_back(directories_json.get_item_string(i));

    return directories;
}

json_obj archiver::read_file_table(std::string file_to_read)
{
    pe_resource_reader res_reader(file_to_read);
    auto res = res_reader.open_resource("little_wrapper", "filetable");
    json_obj json = json_obj(string((char*) res.data, res.size));
    res_reader.close();
    return json;
}