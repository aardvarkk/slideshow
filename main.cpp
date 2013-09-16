#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

#include <Magick++.h>

static const int kFrameRate = 24;
static const char* kFormat = "%08d.png";

typedef std::vector<std::string> Strings;
typedef std::list<Magick::Image> ImageList;

Strings GetFilenames(std::string const& dir, std::string const& ext)
{
  Strings filenames;

  #ifdef _WIN32
  WIN32_FIND_DATA find_data;
  HANDLE h = FindFirstFile(std::string(dir + "\\*." + ext).c_str(), &find_data);
  if (h != INVALID_HANDLE_VALUE) {
    filenames.push_back(dir + "\\" + find_data.cFileName);
    while (FindNextFile(h, &find_data)) {
      filenames.push_back(dir + "\\" + find_data.cFileName);
    }
  }
  #endif

  return filenames;
}

std::string ExecuteCommand(std::string const& cmd)
{
  system((cmd + " >out.txt 2>&1").c_str());
  return std::string();
}

int main(int argc, char* argv[])
{
  // NOTE: No spaces allowed in paths, because that's what FFMPEG demands!
  Strings pictures = GetFilenames("C:\\Users\\Public\\Pictures\\Sample Pictures", "jpg");
  Strings songs    = GetFilenames("C:\\Users\\clarkson\\Desktop\\slideshow\\bin", "mp3");
  
  std::stringstream cmd;
  cmd << "ffmpeg -y -i \"concat:";
  for (size_t i = 0; i < songs.size(); ++i) {
    if (i > 0) cmd << "|";
    cmd << songs[i];
  }
  cmd << "\" output.wav";
  ExecuteCommand(cmd.str());
  //std::cout << res.str() << std::endl;

  // Get music duration
  double duration = 0;
  ExecuteCommand("ffprobe output.wav");

  Magick::InitializeMagick(*argv);
  
  for (size_t i = 0; i < pictures.size(); ++i) {
    Magick::Image img;
    img.read(pictures[i]);

    char filename[0xFF];
    sprintf(filename, kFormat, i);
    img.write(filename);
  }
    
  std::stringstream ss;
  ss << "ffmpeg" 
    << " -i " << kFormat
    << " -r " << kFrameRate 
    << " -c:v libx264"
    << " -pix_fmt yuv420p"
    << " out.mp4"
    << std::endl;
  
  int rc = system(ss.str().c_str());
  return rc;
}