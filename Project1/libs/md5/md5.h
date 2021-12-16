#pragma once

// ��Դ: https://blog.csdn.net/wudishine/article/details/42466831

#include <string>
#include <fstream>
 
/* Type define */
typedef unsigned char ubyte;
typedef unsigned int ulong; // ulong��64λϵͳ��8�ֽڵģ�����ֵ���ԣ�����unsigned int��OK�ˣ���Ϊ�������MD5����ȷ��
 
using std::string;
using std::ifstream;
 
/* MD5 declaration. */
class MD5 {
public:
	MD5();
	MD5(const void *input, size_t length);
	MD5(const string &str);
	MD5(ifstream &in);
	void update(const void *input, size_t length);
	void update(const string &str);
	void update(ifstream &in);
	const ubyte* digest();
	string toString();
	void reset();
private:
	void update(const ubyte *input, size_t length);
	void final();
	void transform(const ubyte block[64]);
	void encode(const ulong *input, ubyte *output, size_t length);
	void decode(const ubyte *input, ulong *output, size_t length);
	string bytesToHexString(const ubyte *input, size_t length);
 
	/* class uncopyable */
	MD5(const MD5&);
	MD5& operator=(const MD5&);
private:
	ulong _state[4];	/* state (ABCD) */
	ulong _count[2];	/* number of bits, modulo 2^64 (low-order word first) */
	ubyte _buffer[64];	/* input buffer */
	ubyte _digest[16];	/* message digest */
	bool _finished;		/* calculate finished ? */
 
	static const ubyte PADDING[64];	/* padding for calculate */
	static const char HEX[16];
	static const size_t BUFFER_SIZE = 1024;
};
