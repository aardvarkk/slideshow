#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

#include <opencv2/opencv.hpp>

static const int    kFrameRate = 24;
static const double kTransitionPerc = 0.2; // This is a fraction of entire seconds per picture, so at 0.1, transition will take 10% of full picture time on each side
static const char*  kFormat = "frames/%08d.png";
static const int    kOutputW = 1920;
static const int    kOutputH = 1080;
static const int    kMaxOutputDim = std::max(kOutputW, kOutputH);
static const int    kMinOutputDim = std::min(kOutputW, kOutputH);
static const double kMinScale = 0.9;
static const double kAspect = static_cast<double>(kOutputW) / kOutputH;

static std::tr1::mt19937 eng_;

typedef std::vector<std::string> Strings;
typedef std::vector<cv::Mat>     Images;

struct LayoutEntry
{
  std::string path;
  cv::Mat img;
  int dim;
  int pos;
};

typedef std::deque<LayoutEntry> Layout;

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

void ConcatenateMusic(Strings const& songs)
{
  std::stringstream cmd;
  cmd << "ffmpeg -y -i \"concat:";
  for (size_t i = 0; i < songs.size(); ++i) {
    if (i > 0) cmd << "|";
    cmd << songs[i];
  }
  cmd << "\" output.wav";
  ExecuteCommand(cmd.str());
}

void WriteFrame(cv::Mat const& img, int num)
{
  char filename[0xFF];
  sprintf(filename, kFormat, num);
  cv::imwrite(
    filename, 
    img
  );
}

cv::Rect RandRect(cv::Mat const& img, double aspect, int min_dim)
{
  int x, y, w, h;
  bool valid = false;

  std::tr1::uniform_int<> min_d(min_dim, std::min(img.rows, img.cols) - 1);
    
  if (aspect > 1) {
    h = min_d(eng_);
    w = static_cast<int>(h * aspect + 0.5);
  } else {
    w = min_d(eng_);
    h = static_cast<int>(w * aspect + 0.5);
  }

  std::tr1::uniform_int<> rnd_x(0, img.cols - w);
  x = rnd_x(eng_);

  std::tr1::uniform_int<> rnd_y(0, img.rows - h);
  y = rnd_y(eng_);

  return cv::Rect(x, y, w, h);
}

cv::Rect LinearInterpRect(cv::Rect const& start, cv::Rect const& end, int frame, int total)
{
  cv::Rect ret = start;
  ret.x += (end.x - start.x) * frame / total;
  ret.y += (end.y - start.y) * frame / total;
  ret.width += (end.width - start.width) * frame / total;
  ret.height += (end.height - start.height) * frame / total;
  return ret;
}

Layout GetLayout(Strings const& pictures, int output_w, int output_h)
{
  Layout layout;

  int prv_pos = 0;

  // Add a starting image
  LayoutEntry start;
  start.dim = std::max(output_w, output_h);
  start.pos = prv_pos;
  prv_pos += start.dim;
  layout.push_back(start);

  for (size_t i = 0; i < pictures.size(); ++i) {
    cv::Mat img = cv::imread(pictures[i]);

    // Scale the image so that its min dimension is equal to
    double scale = static_cast<double>(kMinOutputDim) / std::max(img.rows, img.cols);
    cv::resize(img, img, cv::Size(), scale, scale);

    LayoutEntry entry;
    entry.dim = output_w > output_h ? img.cols : img.rows;
    entry.pos = prv_pos;
    entry.path = pictures[i];
    prv_pos += entry.dim;
    layout.push_back(entry);

    std::cout << "Added " << pictures[i] << " to layout" << std::endl;
  }

  // Add an ending image
  LayoutEntry end;
  end.dim = start.dim;
  end.pos = prv_pos;
  layout.push_back(end);

  return layout;
}

int main(int argc, char* argv[])
{
  // NOTE: No spaces allowed in paths, because that's what FFMPEG demands!
  Strings pictures = GetFilenames("pictures_quick", "jpg");
  Strings songs    = GetFilenames("music", "mp3");
  
  // Stitch the music together into a WAV file (to help determine actual length)
  //ConcatenateMusic(songs);

  // Get the duration of the music
  double duration;
  GetDuration(duration);
  if (duration == 0) {
    std::cerr << "Unable to determine duration of music" << std::endl;
    return EXIT_FAILURE;
  }

  // We know the framerate, so convert the duration into a number of frames
  int frames = static_cast<int>(duration * kFrameRate + 0.5);

  // Find out how much space is required for each image
  Layout layout = GetLayout(pictures, kOutputW, kOutputH);

  // Find out how many pixels we need to move by the last frame
  int total_px = 0;
  for (size_t i = 0; i < layout.size(); ++i) {
    total_px += layout[i].dim;
  }
  // We only need to slide this many pixels through the show
  int slide_px = total_px - 2 * std::max(kOutputW, kOutputH);

  // Our composite (output) frame
  cv::Mat comp(kOutputH, kOutputW, CV_8UC3);
  comp.setTo(0);

  // Go through the frames
  for (int i = 0; i < frames; ++i) {
    int slid = i * slide_px / frames;

    // Go through the layout... if the image is visible, load it
    // If it's no longer visible, unload it
    for (size_t j = 0; j < layout.size(); ++j) {
      int cur_pos = layout[j].pos - slid;

      // This image is invisible
      // Not visible (either future or past), so clear it!
      if (cur_pos >= kMaxOutputDim || cur_pos + layout[j].dim < 0) {
        layout[j].img = cv::Mat();
      } else {

        // We haven't already loaded it...
        if (layout[j].img.empty()) {
          // An endcap image...
          if (layout[j].path.empty()) {
            layout[j].img = cv::Mat(kOutputH, kOutputW, CV_8UC3);
            layout[j].img.setTo(0);
          } 
          // A normal image
          else {
            layout[j].img = cv::imread(layout[j].path);
          }
        }

        int start_comp_px   = std::max(cur_pos, 0);
        int end_comp_px     = std::min(cur_pos + layout[j].dim - 1, kMaxOutputDim-1);
        int start_layout_px = std::max(0, cur_pos - layout[j].pos);
        int end_layout_px   = start_layout_px + (end_comp_px - start_comp_px);
        
        layout[j].img(cv::Rect(start_layout_px, 0, end_layout_px - start_layout_px + 1, kOutputH - 1)).copyTo(
          comp(cv::Rect(start_comp_px, 0, end_comp_px - start_comp_px + 1, kOutputH - 1)));
      }
    }

    // Write it out
    WriteFrame(comp, i);
  }

  //std::stringstream ss;
  //ss << "ffmpeg -y" 
  //  << " -i " << kFormat
  //  << " -r " << kFrameRate 
  //  << " -c:v libx264"
  //  << " -pix_fmt yuv420p"
  //  << " out.mp4"
  //  << std::endl;
  //int rc = system(ss.str().c_str());

  return EXIT_SUCCESS;
}