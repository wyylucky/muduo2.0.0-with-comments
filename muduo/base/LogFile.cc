// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/base/LogFile.h>

#include <muduo/base/FileUtil.h>
#include <muduo/base/ProcessInfo.h>

#include <assert.h>
#include <stdio.h>
#include <time.h>

using namespace muduo;

LogFile::LogFile(const string& basename,
                 off_t rollSize,
                 bool threadSafe,
                 int flushInterval,
                 int checkEveryN)
  : basename_(basename),
    rollSize_(rollSize),
    flushInterval_(flushInterval),
    checkEveryN_(checkEveryN),
    count_(0),
    mutex_(threadSafe ? new MutexLock : NULL),
    startOfPeriod_(0),
    lastRoll_(0),
    lastFlush_(0)
{
  // 断言basename不包含/
  assert(basename.find('/') == string::npos);
  // 产生一个文件
  rollFile();
}

LogFile::~LogFile() = default;

void LogFile::append(const char* logline, int len)
{
  if (mutex_)
  {
    MutexLockGuard lock(*mutex_);
    append_unlocked(logline, len);
  }
  else
  {
    append_unlocked(logline, len);
  }
}

void LogFile::flush()
{
  if (mutex_)
  {
    MutexLockGuard lock(*mutex_);
    file_->flush();
  }
  else
  {
    file_->flush();
  }
}

void LogFile::append_unlocked(const char* logline, int len)
{
  file_->append(logline, len);

// 写得字节数大于rollSize时滚动日志
  if (file_->writtenBytes() > rollSize_)
  {
    rollFile();
  }
  else //没有超过字节数时是否需要滚动日志
  {
    ++count_;
    // 计数值是否超过checkEveryN_，超过了也要滚动
    if (count_ >= checkEveryN_)
    {
      count_ = 0;
      time_t now = ::time(NULL);
      time_t thisPeriod_ = now / kRollPerSeconds_ * kRollPerSeconds_;
      if (thisPeriod_ != startOfPeriod_)//第二天零点，滚动
      {
        rollFile();
      }
      else if (now - lastFlush_ > flushInterval_)//不滚动，是否超过了flush的间隔，超过了就flush
      {
        lastFlush_ = now;
        file_->flush();
      }
    }
  }
}

bool LogFile::rollFile()
{
  time_t now = 0;
  // 根据basename_获取一个文件名称，并把时间返回到now
  string filename = getLogFileName(basename_, &now);

  //做取整操作，对齐为kRollPerSeconds_的整数倍，不一定为now；调整为当天的0点
  time_t start = now / kRollPerSeconds_ * kRollPerSeconds_; 

  if (now > lastRoll_)
  {
    lastRoll_ = now;
    lastFlush_ = now;
    startOfPeriod_ = start;
    file_.reset(new FileUtil::AppendFile(filename));
    return true;
  }
  return false;
}

string LogFile::getLogFileName(const string& basename, time_t* now)
{
  string filename;
  // 先保留basename.size(),再加64长度个空间，足够存放日志文件名
  filename.reserve(basename.size() + 64);
  filename = basename;

  char timebuf[32];
  struct tm tm;
  *now = time(NULL); //获取当前时间，距离1970.1.1 00：00的秒数
  gmtime_r(now, &tm); // FIXME: localtime_r ? //GMT时间 UTC时间； 
  // gmtime_r线程安全，返回时还会保存到参数中；gmtime非线程安全，gmtime返回一个指针，指向一个缓存区，返回时可能被另外一个线程更改。

  strftime(timebuf, sizeof timebuf, ".%Y%m%d-%H%M%S.", &tm);///按照.%Y%m%d-%H%M%S.格式化时间串
  filename += timebuf;

  filename += ProcessInfo::hostname();//获取主机名

  char pidbuf[32];
  snprintf(pidbuf, sizeof pidbuf, ".%d", ProcessInfo::pid());
  filename += pidbuf;

  filename += ".log";

  return filename;
}

