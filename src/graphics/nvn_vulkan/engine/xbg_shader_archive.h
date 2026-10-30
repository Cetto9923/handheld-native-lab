#pragma once
// Local XGS archive reader. The enclosing data contract authenticates the entire file.
// All fields are little endian; keys are sorted 16-character lowercase hex strings.
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

class XbgShaderArchive {
    struct Entry { std::array<char,16> key; uint64_t offset; uint64_t length; };
    int fd_ = -1;
    std::vector<Entry> entries_;
    static uint32_t u32(const unsigned char *p) {
        return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
    }
    static uint64_t u64(const unsigned char *p) {
        return uint64_t(u32(p)) | uint64_t(u32(p+4))<<32;
    }
    static bool read_at(int fd, void *buffer, size_t size, uint64_t pos) {
        auto *p=static_cast<unsigned char *>(buffer);
        while (size) {
            ssize_t n=pread(fd,p,size,static_cast<off_t>(pos));
            if (n<0 && errno==EINTR) continue;
            if (n<=0) return false;
            p+=size_t(n); size-=size_t(n); pos+=uint64_t(n);
        }
        return true;
    }
    static bool valid_key(const char *p) {
        for (int i=0;i<16;i++) if (!((p[i]>='0' && p[i]<='9') || (p[i]>='a' && p[i]<='f'))) return false;
        return true;
    }
public:
    enum class Result { Found, Missing, Invalid };
    XbgShaderArchive() = default;
    XbgShaderArchive(const XbgShaderArchive&) = delete;
    XbgShaderArchive& operator=(const XbgShaderArchive&) = delete;
    ~XbgShaderArchive() { if (fd_>=0) close(fd_); }
    // One caller initializes before readers start. An invalid or absent file is never retained.
    bool open(const char *path) {
        if (fd_>=0) return false;
        int fd=::open(path,O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd<0) return false;
        struct stat st{}; unsigned char header[16];
        if (fstat(fd,&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<16 ||
            uint64_t(st.st_size)>64ull*1024*1024*1024 || !read_at(fd,header,sizeof(header),0) ||
            memcmp(header,"HNLXGS1\0",8)!=0 || u32(header+12)!=0) { close(fd); return false; }
        uint32_t count=u32(header+8);
        uint64_t index_end=16ull+uint64_t(count)*32;
        if (!count || count>1000000 || index_end>uint64_t(st.st_size)) { close(fd); return false; }
        std::vector<unsigned char> index(size_t(count)*32);
        if (!read_at(fd,index.data(),index.size(),16)) { close(fd); return false; }
        std::vector<Entry> parsed; parsed.reserve(count);
        uint64_t next=index_end;
        for (uint32_t i=0;i<count;i++) {
            const unsigned char *raw=index.data()+size_t(i)*32;
            Entry e{}; memcpy(e.key.data(),raw,16); e.offset=u64(raw+16); e.length=u64(raw+24);
            if (!valid_key(e.key.data()) || (!parsed.empty() && !(parsed.back().key<e.key)) ||
                e.offset!=next || e.length<32 || e.length>16ull*1024*1024 || e.length>uint64_t(st.st_size)-next) { close(fd); return false; }
            next+=e.length; parsed.push_back(e);
        }
        if (next!=uint64_t(st.st_size)) { close(fd); return false; }
        entries_.swap(parsed); fd_=fd; return true;
    }
    Result read(const std::string &key,std::vector<uint8_t> &out) const {
        if (fd_<0 || key.size()!=16 || !valid_key(key.data())) return Result::Invalid;
        std::array<char,16> k{}; memcpy(k.data(),key.data(),16);
        auto it=std::lower_bound(entries_.begin(),entries_.end(),k,[](const Entry &a,const std::array<char,16> &b){return a.key<b;});
        if (it==entries_.end() || it->key!=k) return Result::Missing;
        std::vector<uint8_t> data(static_cast<size_t>(it->length));
        if (!read_at(fd_,data.data(),data.size(),it->offset)) return Result::Invalid;
        out.swap(data); return Result::Found;
    }
};
