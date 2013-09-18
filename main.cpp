#include <cstdlib>
#include <fstream>
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
  std::ifstream file("out.txt");
  std::stringstream buffer;
  if (file)
  {
    buffer << file.rdbuf();
    file.close();
  }
  return buffer.str();
}

void GetDuration(double& duration)
{
  // Get music duration
  duration = 0;
  // Sample: Duration: 00:11:57.93
  std::string duration_str = ExecuteCommand("ffprobe output.wav");
  std::string search_str = "Duration:";
  int pos = duration_str.find(search_str);
  if (pos == std::string::npos) {
    return;
  }
  int h, m, s, fs;
  {
    std::stringstream ss;
    ss.str(duration_str.substr(pos + search_str.length()));
    ss >> h;
    ss.ignore();
    ss >> m;
    ss.ignore();
    ss >> s;
    ss.ignore();
    ss >> fs;
  }
  duration = h * 3600 + m * 60 + s + static_cast<double>(fs) / 100;
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

  // Get the duration of the music
  double duration;
  GetDuration(duration);
  if (duration == 0) {
    std::cerr << "Unable to determine duration of music" << std::endl;
    return EXIT_FAILURE;
  }

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