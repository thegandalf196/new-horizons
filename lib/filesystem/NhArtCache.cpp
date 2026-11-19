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
#include <sys/file.h>
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
			/// Removes one directory entry by name. Never follows a link: a linked
			/// name is removed itself, never its target.
			void removeFile(const std::string & name)
			{
#ifdef _WIN32
				check(DeleteFileW((path/name).c_str()));
#else
				check(::unlinkat(handle,name.c_str(),0)==0);
#endif
			}
			void publish(Directory & from,const std::string & source,const std::string & target)
			{
#ifdef _WIN32
				check(MoveFileExW((from.path/source).c_str(),(path/target).c_str(),MOVEFILE_WRITE_THROUGH));
#else
				// Exclusive preparation lock excludes cooperating writers. Destination
				// must be absent; never replace an existing or unrelated cache.
				struct stat info
				{
				};
				check(::fstatat(handle,target.c_str(),&info,AT_SYMLINK_NOFOLLOW)<0 && errno==ENOENT);
				check(::renameat(from.handle,source.c_str(),handle,target.c_str())==0);
				check(::fsync(handle)==0 && ::fsync(from.handle)==0);
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
			for(const auto & entry:boost::filesystem::directory_iterator(boost::filesystem::path(directory.path.native())))
			check(expected.erase(entry.path().filename().string())==1,"Unexpected file or directory in NHART cache");
			check(expected.empty(),"Missing NHART cache inventory file");
		}
		/// Operating-system lock on a regular file: the kernel releases it when the
		/// owning process exits for any reason (crash, kill, Ctrl+C), so an
		/// interrupted preparation can never block a later launch.
		class PreparationLock
		{
#ifdef _WIN32
			HANDLE handle=INVALID_HANDLE_VALUE;
			bool tryAcquire(Directory & parent,const std::string & name)
			{
				// Exclusive share mode is the lock; delete-on-close removes the file
				// when the handle is closed, including by process termination.
				handle=CreateFileW((parent.path/name).c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,OPEN_ALWAYS,
				FILE_ATTRIBUTE_NORMAL|FILE_FLAG_DELETE_ON_CLOSE|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
				if(handle==INVALID_HANDLE_VALUE)
				{
					const auto error=GetLastError();
					check(error==ERROR_SHARING_VIOLATION || error==ERROR_ACCESS_DENIED,"Cannot open NHART cache preparation lock");
					return false;
				}
				BY_HANDLE_FILE_INFORMATION info
				{
				};
				check(GetFileInformationByHandle(handle,&info) && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)));
				return true;
			}
#else
			int handle=-1;
			bool tryAcquire(Directory & parent,const std::string & name)
			{
				if(handle<0)
				{
					handle=::openat(parent.handle,name.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
					check(handle>=0,"Cannot open NHART cache preparation lock (permissions or link)");
					struct stat info
					{
					};
					check(::fstat(handle,&info)==0 && S_ISREG(info.st_mode) && info.st_nlink==1 && info.st_uid==::geteuid());
				}
				if(::flock(handle,LOCK_EX|LOCK_NB)==0)return true;
				check(errno==EWOULDBLOCK || errno==EINTR,"Cannot lock NHART cache preparation");
				return false;
			}
#endif
		public:
			PreparationLock(Directory & parent,const std::string & name)
			{
				const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
				try
				{
					while(!tryAcquire(parent,name))
					{
						check(std::chrono::steady_clock::now()<deadline,
							"NHART cache preparation is busy: another New Horizons process is preparing artwork for this profile");
						std::this_thread::sleep_for(std::chrono::milliseconds(50));
					}
				}
				catch(...)
				{
					close();
					throw;
				}
			}
			~PreparationLock()
			{
				close();
			}
		private:
			void close() noexcept
			{
#ifdef _WIN32
				if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
#else
				if(handle>=0)::close(handle);
#endif
			}
			PreparationLock(const PreparationLock &)=delete;
			PreparationLock & operator=(const PreparationLock &)=delete;
		};
		bool isPayloadName(std::string_view name)
		{
			constexpr std::string_view prefix="payload-";
			return name.starts_with(prefix) && name.size()>prefix.size()
				&& std::all_of(name.begin()+prefix.size(),name.end(),[](char c){return c>='0' && c<='9';});
		}

		constexpr std::string_view stageMarker=".stage-";
		constexpr std::string_view stagePattern="%%%%-%%%%-%%%%-%%%%";

		bool isPreparationStage(std::string_view name)
		{
			constexpr size_t digestLength=64;
			const auto isHex=[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');};
			if(name.size()!=digestLength+stageMarker.size()+stagePattern.size()
				|| name.substr(digestLength,stageMarker.size())!=stageMarker
				|| !std::all_of(name.begin(),name.begin()+digestLength,isHex))
				return false;

			const auto suffix=name.substr(digestLength+stageMarker.size());
			return std::equal(suffix.begin(),suffix.end(),stagePattern.begin(),
				[&](char actual,char expected){return expected=='%' ? isHex(actual) : actual==expected;});
		}

		size_t removeStagePayloads(Directory & root,const std::string & name)
		{
			Directory stage(root.path/name,false);
			std::vector<std::string> payloads;
			for(const auto & entry:boost::filesystem::directory_iterator(boost::filesystem::path(stage.path.native())))
			{
				const auto file=entry.path().filename().string();
				check(isPayloadName(file) && boost::filesystem::is_regular_file(entry.symlink_status()));
				payloads.push_back(file);
			}
			for(const auto & file:payloads)
				stage.removeFile(file);
			return payloads.size();
		}

		// Only scan the staging root owned by the current lock protocol.
		void removeAbandonedEntries(Directory & root)
		{
			std::vector<std::string> stages;
			for(const auto & entry:boost::filesystem::directory_iterator(boost::filesystem::path(root.path.native())))
			{
				const auto name=entry.path().filename().string();
				if(isPreparationStage(name) && boost::filesystem::is_directory(entry.symlink_status()))
					stages.push_back(name);
			}
			for(const auto & name:stages)
			{
				try
				{
					const auto count=removeStagePayloads(root,name);
					// On Windows the directory handle must close before removal.
					root.removeEmpty(name);
					logGlobal->info("Removed partial NHART cache left by an interrupted launch (%zu files)",count);
				}
				catch(const std::exception &)
				{
					logGlobal->warn("Retaining unexpected NHART cache entry %s",name);
				}
			}
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
		Directory artRoot(fs::path(cacheRoot.native())/"new-horizons-art",true);
		Directory root(artRoot.path/"v1",true);
		Directory stages(artRoot.path/"v1.staging",true);
#ifndef _WIN32
		for(const auto * owned:{&artRoot,&root,&stages})
		{
			struct stat rootInfo
			{
			};
			check(::fstat(owned->handle,&rootInfo)==0 && rootInfo.st_uid==::geteuid() && !(rootInfo.st_mode&(S_IWGRP|S_IWOTH)));
		}
#endif
		// Kept beside, not inside, v1 so the version directory holds only caches.
		PreparationLock lock(artRoot,"v1.prepare.lock");
		removeAbandonedEntries(stages);
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
		// An interrupted stage remains for the next lock owner to clear.
		const auto cache=root.path/identity;
		if(boost::filesystem::exists(boost::filesystem::symlink_status(boost::filesystem::path(cache.native()))))
		{
			Directory existing(cache,false);
			verifyFiles(existing,records);
			logGlobal->info("Reusing verified NHART cache (%zu records)",records.size());
			return finish(cache);
		}
		const auto stage=identity+std::string(stageMarker)+boost::filesystem::unique_path(std::string(stagePattern)).string();
		check(stages.make(stage));
		logGlobal->info("Preparing NHART disk cache (%zu records)",records.size());
		{
			Directory staging(stages.path/stage,false);
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
		root.publish(stages,stage,identity);
		logGlobal->info("Prepared verified NHART cache (%zu records)",records.size());
		return finish(cache);
	}
}
