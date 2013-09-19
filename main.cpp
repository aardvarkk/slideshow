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
static const double kMinScale = 0.9;
static const double kAspect = static_cast<double>(kOutputW) / kOutputH;

static std::tr1::mt19937 eng_;

typedef std::vector<std::string> Strings;
typedef std::vector<cv::Mat>     Images;

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

// For a bad image, no fading occurs -- just sliding
void BadImage(cv::Mat const& prv, cv::Mat const& cur, int to_use, int transition_frames, int* frame) 
{
  // Pick a start & end box of the "bad" aspect on the image
  cv::Rect start_r = RandRect(cur, static_cast<double>(kOutputH) / kOutputW, std::min(cur.rows, cur.cols) * kMinScale);
  cv::Rect end_r   = RandRect(cur, static_cast<double>(kOutputH) / kOutputW, std::min(cur.rows, cur.cols) * kMinScale);

  // Required space after slide
  int required_px = static_cast<int>(std::min(kOutputW, kOutputH) / kAspect);

  // An output frame
  cv::Mat comp = cv::Mat(kOutputW, kOutputH, CV_8UC3);

  // How many frames we've used so far
  int used = 0;

  // Slide in
  for (; used < transition_frames; ++used) {
    int trans_loc = kOutputW - required_px * used / transition_frames;
    cv::Rect crop = LinearInterpRect(start_r, end_r, used, to_use);

    prv.copy

    WriteFrame(comp, *frame++);
  }

  // Stay solid
  for (; used < to_use; ++used) {
    WriteFrame(comp, *frame++);
  }
}

int main(int argc, char* argv[])
{
  // NOTE: No spaces allowed in paths, because that's what FFMPEG demands!
  Strings pictures = GetFilenames("pictures", "jpg");
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
  double seconds_per_pic = duration / pictures.size();
  double transition_seconds = seconds_per_pic * kTransitionPerc;
  int transition_frames = static_cast<int>(transition_seconds * kFrameRate + 0.5);

  // We need to save some frames at the end for a fadeout
  // Otherwise, every image is just a transition in, and then the image itself
  frames -= transition_frames;

  // Get a black image of the correct size
  cv::Mat black(kOutputH, kOutputW, CV_8UC3);
  black.setTo(0);

  // Start out saying our previous image is just black
  cv::Mat prv = black;

  // Start dealing with each picture
  int frame = 0;
  for (size_t i = 0; i < pictures.size(); ++i) {
    cv::Mat cur = cv::imread(pictures[i]);
    double aspect1 = static_cast<double>(cur.cols) / cur.rows;
    double aspect2 = static_cast<double>(cur.rows) / cur.cols;

    // Good image if our aspect is similar to the output aspect
    bool good = (aspect1 > 1) == (kAspect > 1);

    // How many frames will we give this image?
    size_t start_frame = i * frames / pictures.size();
    size_t end_frame   = (i+1) * frames / pictures.size() - 1;

    if (good) {
    } else {
      BadImage(prv, cur, end_frame - start_frame + 1, transition_frames, &frame);
    }
  }

  // Do the first fade up
  //int frame = 0;
  //cv::Mat first = cv::imread(pictures.front());
  //for (; frame < transition_frames; ++frame) {
  //  double alpha = static_cast<double>(frame) / transition_frames;
  //  WriteFrame(
  //    alpha * first(cv::Rect(0, 0, kOutputW, kOutputH)) + (1-alpha) * black,
  //    frame
  //    );
  //}

  //// Go through all frames
  //for (int i = 0; i < frames; ++i) {
  //  double cur_time = static_cast<double>(i) / frames * duration;

  //  static int prev_image_idx = 0;
  //  int image_idx = i * pictures.size() / frames;

  //  // We're starting a transition
  //  if (image_idx != prev_image_idx) {
  //  }

  //}

  //Images images(pictures.size());
  //for (size_t i = 1; i < pictures.size(); ++i) {
  //  images[i] = cv::imread(pictures[i]);

  //}
    
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