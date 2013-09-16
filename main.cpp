#include <cstdlib>
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

int main(int argc, char* argv[])
{
  Magick::InitializeMagick(*argv);
  
  Strings filenames = GetFilenames("C:\\Users\\Public\\Pictures\\Sample Pictures", "jpg");
  
  for (size_t i = 0; i < filenames.size(); ++i) {
    Magick::Image img;
    img.read(filenames[i]);

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