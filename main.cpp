#include <cstdlib>
#include <sstream>

#include <Magick++.h>

static const int kFrameRate = 24;

int main(int argc, char* argv[])
{
  Magick::InitializeMagick(*argv);
  Magick::Image img;
  img.read("C:\\Users\\Public\\Pictures\\Sample Pictures\\Chrysanthemum.jpg");
  img.write("img0000.png");

  std::stringstream ss;
  ss << "ffmpeg" 
    << " -i img%04d.png"
    << " -r " << kFrameRate 
    << " -c:v libx264"
    << " -pix_fmt yuv420p"
    << " out.mp4"
    << std::endl;
  
  int rc = system(ss.str().c_str());
  return rc;
}