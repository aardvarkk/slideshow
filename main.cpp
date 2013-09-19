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
static const double kMinScale = 0.8;
static const double kAspect = static_cast<double>(kOutputW) / kOutputH;

#define MAKE_FRAMES

static std::tr1::mt19937 eng_;

typedef std::vector<std::string> Strings;
typedef std::vector<cv::Mat>     Images;

struct LayoutEntry
{
  std::string path;
  cv::Mat img;
  int dim;
  int pos;
  cv::Rect_<double> start_r;
  cv::Rect_<double> end_r;
};

typedef std::deque<LayoutEntry> Layout;

Strings GetFilenames(std::string const& dir, std::string const& ext)
{
  Strings filenames;

  #ifdef _WIN32
  WIN32_FIND_DATA find_data;
  std::string search = dir + "\\*." + ext;
  HANDLE h = FindFirstFile(search.c_str(), &find_data);
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

cv::Rect_<double> RandRect(cv::Mat const& img, int output_w, int output_h, double min_scale)
{
  double x, y, w, h;
  bool valid = false;

  double min_dim = min_scale * std::min(img.rows, img.cols);
  std::tr1::uniform_real<> min_d(min_dim, std::min(img.rows, img.cols) - 1);
    
  if (kOutputW > kOutputH) {
    h = min_d(eng_);
    w = h * img.cols / img.rows;
  } else {
    w = min_d(eng_);
    h = w * img.rows / img.cols;
  }

  std::tr1::uniform_real<> rnd_x(0, img.cols - w);
  x = rnd_x(eng_);

  std::tr1::uniform_real<> rnd_y(0, img.rows - h);
  y = rnd_y(eng_);

  return cv::Rect_<double>(x, y, w, h);
}

cv::Rect_<double> LinearInterpRect(cv::Rect const& start, cv::Rect const& end, double alpha)
{
  cv::Rect_<double> ret = start;
  ret.x += (end.x - start.x) * alpha;
  ret.y += (end.y - start.y) * alpha;
  ret.width  += (end.width - start.width)   * alpha;
  ret.height += (end.height - start.height) * alpha;
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

    // Scale the image so that its fits on the sliding timeline
    double scale = 0;
    if (output_w > output_h) {
      scale = static_cast<double>(kOutputH) / img.rows;
    } else {
      scale = static_cast<double>(kOutputW) / img.cols;
    }
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
  Strings songs    = GetFilenames("music_quick", "mp3");
  
  // Stitch the music together into a WAV file (to help determine actual length)
  ConcatenateMusic(songs);

  // Get the duration of the music
  double duration;
  GetDuration(duration);
  if (duration == 0) {
    std::cerr << "Unable to determine duration of music" << std::endl;
    return EXIT_FAILURE;
  }

  #ifdef MAKE_FRAMES
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
  int slide_px = total_px - std::max(kOutputW, kOutputH);

  // Our composite (output) frame
  cv::Mat comp(kOutputH, kOutputW, CV_8UC3);
  comp.setTo(0);

  // Go through the frames
  for (int i = 0; i < frames; ++i) {
    int slid = i * slide_px / (frames-1);

    // Go through the layout... if the image is visible, load it
    // If it's no longer visible, unload it
    for (size_t j = 0; j < layout.size(); ++j) {
      int cur_pos = layout[j].pos - slid;

      // This image is invisible
      // Not visible (either future or past), so clear it!
      if (cur_pos >= kMaxOutputDim || cur_pos + layout[j].dim - 1 < 0) {
        layout[j].img = cv::Mat();
      } else {

        // We haven't already loaded it...
        if (layout[j].img.empty()) {
          // An endcap image...
          if (layout[j].path.empty()) {
            layout[j].img = cv::Mat(kOutputH, kOutputW, CV_8UC3);
            layout[j].img.setTo(0);
            layout[j].start_r = cv::Rect_<double>(0, 0, kOutputW, kOutputH);
            layout[j].end_r = cv::Rect_<double>(0, 0, kOutputW, kOutputH);
          } 
          // A normal image
          else {
            layout[j].img = cv::imread(layout[j].path);
            layout[j].start_r = RandRect(layout[j].img, kOutputW, kOutputH, kMinScale);
            layout[j].end_r = RandRect(layout[j].img, kOutputW, kOutputH, kMinScale);
          }
        }

        // Interpolate the cropping area
        cv::Rect_<double> src_r = LinearInterpRect(
          layout[j].start_r, 
          layout[j].end_r,
          static_cast<double>(kMaxOutputDim - cur_pos) / (kMaxOutputDim + layout[j].dim)
          );

        cv::Mat cropped = layout[j].img(src_r);
        cv::Size target;
        if (kOutputW > kOutputH) {
          target.width = layout[j].dim;
          target.height = kOutputH;
        } else {
          target.height = layout[j].dim;
          target.width = kOutputW;
        }
        cv::resize(cropped, cropped, target);

        //if (j == 1) {
        //  WriteFrame(cropped, i);
        //}

        int comp_px_s = std::max(cur_pos, 0);
        int comp_px_e = std::min(cur_pos + layout[j].dim - 1, kMaxOutputDim-1);
        int comp_px_w = comp_px_e - comp_px_s + 1;

        int crop_px_s;
        if (cur_pos >= 0) {
          crop_px_s = 0;
        } else {
          crop_px_s = -cur_pos;
        }
        int crop_px_w = std::min(comp_px_w, layout[j].dim);

        std::cout << "Cropping an image of dimension " << cropped.cols << "x" << cropped.rows << " at (" << crop_px_s << ", " << 0 << ", " << crop_px_w << ", " << kOutputH - 1 << ")" << std::endl;
        cropped(cv::Rect(crop_px_s, 0, crop_px_w, kOutputH - 1)).copyTo(
          comp(cv::Rect(comp_px_s, 0, comp_px_w, kOutputH - 1)));
      }
    }

    // Write it out
    WriteFrame(comp, i);

    std::cout << "Wrote frame " << i+1 << " of " << frames << std::endl;
  }
  #endif

  std::stringstream ss;
  ss << "ffmpeg -y" 
    << " -i " << kFormat
    << " -r " << kFrameRate 
    << " -c:v libx264"
    << " -pix_fmt yuv420p"
    << " -tune film"
    << " -crf 18"
    << " out.mp4"
    << std::endl;
  ExecuteCommand(ss.str());

  ExecuteCommand("ffmpeg -y -i out.mp4 -i output.wav -c:v copy final.mp4");

  return EXIT_SUCCESS;
}