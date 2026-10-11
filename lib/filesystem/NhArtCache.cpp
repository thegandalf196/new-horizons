/* Verified pre-game NHART cache, GPL-2.0-or-later. */
#include "StdInc.h"
#include "NhArtCache.h"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace nhart
{
	namespace
	{
		namespace fs = std::filesystem;
		void check(bool valid, const std::string & message = "Unsafe, linked or inaccessible NHART cache path")
		{
			if(!valid)
			throw std::runtime_error(message);
		}
		// Keep ancestor handles open throughout operations. POSIX traverses from / using
		// O_NOFOLLOW/openat; Windows rejects reparse points and denies rename/delete of
		// pinned directories. Never recursively remove directories on failure.
		class Directory
		{
		public:
			fs::path path;
#ifdef _WIN32
			std::vector<HANDLE> ancestors;
			explicit Directory(fs::path location, bool create) : path(fs::absolute(location).lexically_normal())
			{
				try
				{
					fs::path current=path.root_path();
					for(const auto & part : path.relative_path())
					{
						check(part!="." && part!="..");
						current/=part;
						if(create && !CreateDirectoryW(current.c_str(),nullptr)) check(GetLastError()==ERROR_ALREADY_EXISTS);
						const auto handle=CreateFileW(current.c_str(),FILE_READ_ATTRIBUTES,
						FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
						FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
						check(handle!=INVALID_HANDLE_VALUE);
						ancestors.push_back(handle);
						BY_HANDLE_FILE_INFORMATION info
						{
						};
						check(GetFileInformationByHandle(handle,&info));
						check((info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT));
					}
				}
				catch(...)
				{
					for(auto handle:ancestors)CloseHandle(handle);
					throw;
				}
			}
			~Directory()
			{
				for(auto it=ancestors.rbegin();it!=ancestors.rend();++it)CloseHandle(*it);
			}
#else
			int handle=-1;
			explicit Directory(fs::path location,bool create) : path(fs::absolute(location).lexically_normal())
			{
				handle=::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
				try
				{
					check(handle>=0);
					for(const auto & part:path.relative_path())
					{
						const auto name=part.string();
						check(name!="." && name!="..");
						if(create && ::mkdirat(handle,name.c_str(),0700)<0)check(errno==EEXIST);
						const int next=::openat(handle,name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
						check(next>=0);
						::close(handle);
						handle=next;
					}
				}
				catch(...)
				{
					if(handle>=0)::close(handle);
					throw;
				}
			}
			~Directory()
			{
				if(handle>=0)::close(handle);
			}
#endif
			Directory(const Directory &)=delete;
			Directory & operator=(const Directory &)=delete;
			bool make(const std::string & name)
			{
#ifdef _WIN32
				if(CreateDirectoryW((path/name).c_str(),nullptr))return true;
				check(GetLastError()==ERROR_ALREADY_EXISTS);
				return false;
#else
				if(::mkdirat(handle,name.c_str(),0700)==0)return true;
				check(errno==EEXIST);
				return false;
#endif
			}
			void removeEmpty(const std::string & name)
			{
#ifdef _WIN32
				check(RemoveDirectoryW((path/name).c_str()));
#else
				check(::unlinkat(handle,name.c_str(),AT_REMOVEDIR)==0);
#endif
			}
			void publish(const std::string & source,const std::string & target)
			{
#ifdef _WIN32
				check(MoveFileExW((path/source).c_str(),(path/target).c_str(),MOVEFILE_WRITE_THROUGH));
#else
				// Exclusive preparation lock excludes cooperating writers. Destination
				// must be absent; never replace an existing or unrelated cache.
				struct stat info
				{
				};
				check(::fstatat(handle,target.c_str(),&info,AT_SYMLINK_NOFOLLOW)<0 && errno==ENOENT);
				check(::renameat(handle,source.c_str(),handle,target.c_str())==0);
				check(::fsync(handle)==0);
#endif
			}
		};
		class File
		{
#ifdef _WIN32
			HANDLE handle=INVALID_HANDLE_VALUE;
#else
			int handle=-1;
#endif
		public:
			File(Directory & parent,const std::string & name,bool write)
			{
#ifdef _WIN32
				handle=CreateFileW((parent.path/name).c_str(),write?GENERIC_WRITE:GENERIC_READ,
				FILE_SHARE_READ,nullptr,write?CREATE_NEW:OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
				check(handle!=INVALID_HANDLE_VALUE,write ? "Cannot create exclusive NHART cache payload (permissions or occupied path)"
				: "Missing or unreadable NHART source/cache payload");
				BY_HANDLE_FILE_INFORMATION info
				{
				};
				if(!GetFileInformationByHandle(handle,&info) || (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))
				{
					CloseHandle(handle);
					handle=INVALID_HANDLE_VALUE;
					check(false);
				}
#else
				handle=::openat(parent.handle,name.c_str(),(write?O_WRONLY|O_CREAT|O_EXCL:O_RDONLY)|O_NOFOLLOW|O_CLOEXEC,0600);
				check(handle>=0,write ? "Cannot create exclusive NHART cache payload (permissions, link or occupied path)"
				: "Missing, linked or unreadable NHART source/cache payload");
				struct stat info
				{
				};
				if(::fstat(handle,&info)!=0 || !S_ISREG(info.st_mode) || info.st_nlink!=1)
				{
					::close(handle);
					handle=-1;
					check(false);
				}
#endif
			}
			~File()
			{
#ifdef _WIN32
				if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
#else
				if(handle>=0)::close(handle);
#endif
			}
			void seek(ui64 offset)
			{
#ifdef _WIN32
				LARGE_INTEGER at
				{
				};
				at.QuadPart=offset;
				check(SetFilePointerEx(handle,at,nullptr,FILE_BEGIN));
#else
				check(::lseek(handle,offset,SEEK_SET)==static_cast<off_t>(offset));
#endif
			}
			size_t read(ui8 * bytes,size_t size)
			{
#ifdef _WIN32
				DWORD count=0;
				check(ReadFile(handle,bytes,static_cast<DWORD>(size),&count,nullptr));
				return count;
#else
				ssize_t count;
				do
				{
					count=::read(handle,bytes,size);
				}
				while(count<0 && errno==EINTR);
				check(count>=0,"NHART source/cache read failed");
				return count;
#endif
			}
			void write(const ui8 * bytes,size_t size)
			{
				while(size)
				{
#ifdef _WIN32
					DWORD count=0;
					check(WriteFile(handle,bytes,static_cast<DWORD>(size),&count,nullptr));
#else
					ssize_t count;
					do
					{
						count=::write(handle,bytes,size);
					}
					while(count<0 && errno==EINTR);
#endif
					check(count>0,"NHART cache write failed (check free space and permissions)");
					bytes+=count;
					size-=count;
				}
			}
			void flush()
			{
#ifdef _WIN32
				check(FlushFileBuffers(handle));
#else
				check(::fsync(handle)==0);
#endif
			}
		};
		class ReadBuffer : public std::streambuf
		{
			File & file;
			std::array<char,65536> bytes
			{
			};
			protected:
			int_type underflow() override
			{
				const auto count=file.read(reinterpret_cast<ui8 *>(bytes.data()),bytes.size());
				if(!count)return traits_type::eof();
				setg(bytes.data(),bytes.data(),bytes.data()+count);
				return traits_type::to_int_type(*gptr());
			}
		public:
			explicit ReadBuffer(File & file):file(file)
			{
			}
		};
		Digest digest(File & file,ui64 length)
		{
			ReadBuffer buffer(file);
			std::istream stream(&buffer);
			return sha256(stream,length);
		}
		void verifyFiles(Directory & directory,const std::vector<Record> & records)
		{
			std::set<std::string> expected;
			for(size_t i=0;i<records.size();++i)
			{
				const auto name=cacheFileName(i);
				expected.insert(name);
				File file(directory,name,false);
				check(digest(file,records[i].size)==records[i].digest,"NHART cache checksum mismatch: "+name);
				file.seek(records[i].size);
				ui8 extra;
				check(file.read(&extra,1)==0,"NHART cache payload length mismatch: "+name);
			}
			for(const auto & entry:fs::directory_iterator(directory.path))
			check(expected.erase(entry.path().filename().string())==1,"Unexpected file or directory in NHART cache");
			check(expected.empty(),"Missing NHART cache inventory file");
		}
	}
	std::string cacheFileName(size_t record)
	{
		return "payload-"+std::to_string(record);
	}
	std::shared_ptr<const PreparedCache> prepareCache(const boost::filesystem::path & archive,
	const boost::filesystem::path & cacheRoot,const std::vector<Record> & records)
	{
		static std::mutex mutex;
		static std::map<std::pair<std::string,std::string>,std::weak_ptr<const PreparedCache>> prepared;
		std::lock_guard guard(mutex);
		check(records.size()<=1000000);
		const auto key=std::make_pair(boost::filesystem::absolute(archive).lexically_normal().string(),
		boost::filesystem::absolute(cacheRoot).lexically_normal().string());
		const auto size=boost::filesystem::file_size(archive);
		const auto modified=boost::filesystem::last_write_time(archive);
		if(const auto previous=prepared[key].lock())
		{
			check(previous->archiveSize==size && previous->archiveTime==modified && previous->records.size()==records.size());
			for(size_t i=0;i<records.size();++i)
			check(previous->records[i].name==records[i].name && previous->records[i].offset==records[i].offset
			&& previous->records[i].size==records[i].size && previous->records[i].digest==records[i].digest);
			return previous;
		}
		Directory source(fs::path(archive.parent_path().native()),false);
		File input(source,archive.filename().string(),false);
		check(size<=1024ULL*1024*1024,"NHART startup cache supports archives up to 1 GiB");
		logGlobal->info("Checking NHART startup cache (%zu records)",records.size());
		const auto identity=hex(digest(input,size));
		input.seek(0);
		ui8 byte;
		input.seek(size);
		check(input.read(&byte,1)==0);
		Directory root(fs::path(cacheRoot.native())/"new-horizons-art"/"v1",true);
#ifndef _WIN32
		struct stat rootInfo
		{
		};
		check(::fstat(root.handle,&rootInfo)==0 && rootInfo.st_uid==::geteuid() && !(rootInfo.st_mode&(S_IWGRP|S_IWOTH)));
#endif
		const auto lockName=identity+".lock";
		bool locked=false;
		for(int attempt=0;attempt<300 && !locked;++attempt)
		{
			locked=root.make(lockName);
			if(!locked)std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		check(locked,"NHART cache preparation is busy; a crashed preparation may leave a stale hash.lock requiring explicit recovery");
		const auto finish=[&](const fs::path & path)
		{
			auto result=std::make_shared<PreparedCache>(PreparedCache
			{
				boost::filesystem::path(path.native()),size,modified,records
			}
			);
			prepared[key]=result;
			return result;
		};
		try
		{
			const auto cache=root.path/identity;
			if(fs::exists(fs::symlink_status(cache)))
			{
				Directory existing(cache,false);
				verifyFiles(existing,records);
				logGlobal->info("Reusing verified NHART cache (%zu records)",records.size());
				root.removeEmpty(lockName);
				return finish(cache);
			}
			const auto stage=identity+".stage-"+boost::filesystem::unique_path("%%%%-%%%%-%%%%-%%%%").string();
			check(root.make(stage));
			logGlobal->info("Preparing NHART disk cache (%zu records)",records.size());
			{
				Directory staging(root.path/stage,false);
				std::array<ui8,65536> bytes
				{
				};
				for(size_t i=0;i<records.size();++i)
				{
					const auto & entry=records[i];
					check(entry.offset<=size && entry.size<=size-entry.offset);
					input.seek(entry.offset);
					File output(staging,cacheFileName(i),true);
					ui64 remaining=entry.size;
					while(remaining)
					{
						const auto count=input.read(bytes.data(),std::min<ui64>(remaining,bytes.size()));
						check(count>0);
						output.write(bytes.data(),count);
						remaining-=count;
					}
					output.flush();
				}
				verifyFiles(staging,records);
			}
			// A changing input cannot publish mixed payloads under its old hash.
			input.seek(0);
			check(hex(digest(input,size))==identity);
			root.publish(stage,identity);
			root.removeEmpty(lockName);
			logGlobal->info("Prepared verified NHART cache (%zu records)",records.size());
			return finish(cache);
		}
		catch(...)
		{
			root.removeEmpty(lockName);
			// Keep exact owned partial stage for diagnosis; no broad cleanup/deletion.
			throw;
		}
	}
}
