#include <Magick++.h>

int main(int argc, char* argv[])
{
  Magick::InitializeMagick(*argv);
  Magick::Image img;
  img.read("C:\\Users\\Public\\Pictures\\Sample Pictures\\Chrysanthemum.jpg");
}