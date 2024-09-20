#include <bcm2835.h>
#include <cstdio>
#include "SSD1306_OLED.hpp"
#include <iostream>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <chrono>
#include <string>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <thread>
#include <fstream>
#include <sys/statvfs.h>
#define OLEDwidth 128
#define OLEDheight 64
#define FULLSCREEN (OLEDwidth * (OLEDheight / 8))
SSD1306 OLED(OLEDwidth, OLEDheight);

#define BUTTON_UP 14
#define BUTTON_OK 15
#define BUTTON_DOWN 18
bool Setup();
void Loop();
void End();
std::string getCurrentTime();
uint8_t screenBuffer[FULLSCREEN];
std::string strModes[] = {" 1080p@25 + mic", " 1080p@30      ", " 720p@30       ", " Twitch 250k   ", " Twitch 500k   ", " gstreamer     ", " Settings      ", " Power OFF     "};
enum Modes
{
	FULLHD_25_MIC = 0,
	FULLHD_30,
	HD_30,
	TWITCH_250K,
	TWITCH_500K,
	GSTREAMER,
	SETTINGS,
	POWEROFF
};
int main()
{
	if (!Setup())
		return -1;
	Loop();

	End();
	return 0;
}

std::string FormatTime(int seconds)
{
	char timestr[64];
	sprintf(timestr, "%02d:%02d:%02d", seconds / 3600, (seconds % 3600) / 60, seconds % 60);
	return std::string(timestr);
}
bool isProcessRunning(const char *processName)
{
	std::string command = "ps aux | grep -v grep | grep ";
	command += processName;

	if (std::system(command.c_str()) == 0)
	{
		return true; // Process is running
	}
	else
	{
		return false; // Process is not running
	}
}
void Loop()
{
	bool lastUp = true, lastOk = true, lastDown = true;
	Modes mode = FULLHD_25_MIC;
	bool bRecording = false, ffmpeg = false, rpicam = false, arecord = false;
	std::string info;
	std::chrono::high_resolution_clock::time_point start;
	while (true)
	{
		bool newUp = bcm2835_gpio_lev(BUTTON_UP), newOk = bcm2835_gpio_lev(BUTTON_OK), newDown = bcm2835_gpio_lev(BUTTON_DOWN);
		bool bUp = lastUp && !newUp, bOk = lastOk && !newOk, bDown = lastDown && !newDown;

		static int i = 0;
		if (i++ % 100 == 0)
		{
			struct ifaddrs *addrs;
			getifaddrs(&addrs);
			while (addrs)
			{
				if (addrs->ifa_addr && addrs->ifa_addr->sa_family == AF_INET)
				{
					struct sockaddr_in *pAddr = (struct sockaddr_in *)addrs->ifa_addr;
					info = inet_ntoa(pAddr->sin_addr);
				}
				addrs = addrs->ifa_next;
			}
			freeifaddrs(addrs);
			ffmpeg = isProcessRunning("ffmpeg");
			rpicam = isProcessRunning("libcamera-vid");
		}
		else if (i % 100 == 50)
		{
			std::string temp_str;
			std::ifstream temp_file("/sys/class/thermal/thermal_zone0/temp");

			if (!temp_file.is_open())
			{
				std::cerr << "Error: Unable to open temperature file." << std::endl;
			}
			std::getline(temp_file, temp_str);
			temp_file.close();

			struct statvfs buf;
			if (statvfs(".", &buf) < 0)
			{
				printf("Error in statvfs\n");
			}

			unsigned long long total_memory_bytes = buf.f_blocks * (unsigned long long)buf.f_bsize;
			unsigned long long used_memory_bytes = (buf.f_blocks - buf.f_bfree) * (unsigned long long)buf.f_bsize;
			unsigned long long available_memory_bytes = buf.f_bavail * (unsigned long long)buf.f_bsize;
			int total_memory_gb = total_memory_bytes / (1024.0 * 1024.0 * 1024.0);
			int used_memory_gb = used_memory_bytes / (1024.0 * 1024.0 * 1024.0);
			int available_memory_gb = available_memory_bytes / (1024.0 * 1024.0 * 1024.0);
			int percentage_used = ((double)used_memory_gb / (double)total_memory_gb) * 100.0;

			int temp = std::stof(temp_str) / 1000.0;
			info = std::to_string(temp) + "C " + std::to_string(used_memory_gb) + '/' + std::to_string(total_memory_gb) + "GB " + std::to_string(percentage_used) + '%';
		}

		if (bOk)
		{
			if (bRecording)
			{
				std::system("sudo pkill libcamera-vid");
				bcm2835_delay(100);
				std::system("sudo pkill ffmpeg");
				std::cout << "[Stop]\n";
				bRecording = false;
			}
			else if (mode == FULLHD_25_MIC)
			{
				std::string command = "libcamera-vid -n -t 0 --width 1920 --height 1080 --inline --framerate 25 -o - | ffmpeg -re -i - -re -f alsa -ac 1 -itsoffset 0.0 -thread_queue_size 12000 -i plughw:1 -map 0:v:0 -map 1:a:0 -map_metadata:g 1:g -c:v copy -preset ultrafast -r 25 -g 50 -c:a aac -ar 44100 -b:a 128k -filter:a \"volume=12dB\" -f flv /home/pi/Videos/" + getCurrentTime() + ".flv &";
				std::cout << "[Start] 1920x1080@25fps + mic\n";
				bRecording = true;
				start = std::chrono::high_resolution_clock::now();
				int result = std::system(command.c_str());
				if (result != 0)
				{
					std::cerr << "Error\n";
					bRecording = false;
				}
			}
			else if (mode == FULLHD_30)
			{
				std::string command = "libcamera-vid -o /home/pi/Videos/" + getCurrentTime() + ".h264 --width 1920 --height 1080 --framerate 30 -t 0 &";
				std::cout << "[Start] 1920x1080@30fps\n";
				bRecording = true;
				start = std::chrono::high_resolution_clock::now();
				int result = std::system(command.c_str());
				if (result != 0)
				{
					std::cerr << "Error\n";
					bRecording = false;
				}
			}
			else if (mode == HD_30)
			{
				std::string command = "libcamera-vid -o /home/pi/Videos/" + getCurrentTime() + ".h264 --width 1280 --height 720 --framerate 30 -t 0 &";
				std::cout << "[Start] 1280x720@30fps\n";
				bRecording = true;
				start = std::chrono::high_resolution_clock::now();
				int result = std::system(command.c_str());
				if (result != 0)
				{
					std::cerr << "Error\n";
					bRecording = false;
				}
			}
			else if (mode == TWITCH_250K)
			{
				std::string command = "libcamera-vid -n -t 0 --width 1920 --height 1080 --framerate 25 --level 4.2 --denoise cdn_off --inline  -b 2500000  -o - | ffmpeg -re -i - -re -f alsa -ac 1 -itsoffset 0.0 -thread_queue_size 12000 -i plughw:0 -map 0:v:0 -map 1:a:0 -map_metadata:g 1:g -c:v copy -preset ultrafast -r 25 -g 50 -b:v 2500000 -c:a aac -ar 44100 -b:a 128k -filter:a \"volume=20dB\" -f flv \"rtmp://live.twitch.tv/app/live_897960293_8BEI6uaxb4WFaTS6lzD6M0Ar1Czlqi\" &";
				std::cout << "[Start] Twitch 250k 1920x1080@25fps + mic\n";
				bRecording = true;
				start = std::chrono::high_resolution_clock::now();
				int result = std::system(command.c_str());
				if (result != 0)
				{
					std::cerr << "Error\n";
					bRecording = false;
				}
			}
			else if (mode == TWITCH_500K)
			{
				std::string command = "libcamera-vid -n -t 0 --width 1920 --height 1080 --inline --framerate 25 -b 5000000  -o - | ffmpeg -re -i - -re -f alsa -ac 1 -itsoffset 0.0 -thread_queue_size 12000 -i plughw:0 -map 0:v:0 -map 1:a:0 -map_metadata:g 1:g -c:v copy -preset ultrafast -r 25 -g 50 -b:v 5000000 -c:a aac -ar 44100 -b:a 128k -filter:a \"volume=20dB\" -f flv \"rtmp://live.twitch.tv/app/live_897960293_8BEI6uaxb4WFaTS6lzD6M0Ar1Czlqi\" &";
				std::cout << "[Start] Twitch 500k 1920x1080@25fps + mic\n";
				bRecording = true;
				start = std::chrono::high_resolution_clock::now();
				int result = std::system(command.c_str());
				if (result != 0)
				{
					std::cerr << "Error\n";
					bRecording = false;
				}
			}
			else if (mode == GSTREAMER)
			{
				std::string command = "libcamera-vid --width 1920 --height 1080 --framerate 30 -b 2500000 -t 0 -n -o - | gst-launch-1.0 fdsrc ! h264parse ! rtph264pay config-interval=1 pt=96 ! udpsink host=tutos.ddns.net port=2222 &";
				std::cout << "[Start] Gstreamer 1920x1080 30fps\n";
				bRecording = true;
				start = std::chrono::high_resolution_clock::now();
				int result = std::system(command.c_str());
				if (result != 0)
				{
					std::cerr << "Error\n";
					bRecording = false;
				}
			}

			else if (mode == POWEROFF)
			{
				std::cout << "Shutting down\n";
				int result = std::system("sudo shutdown -h now");
				if (result != 0)
				{
					std::cerr << "Error\n";
					bRecording = false;
				}
			}
		}

		if (bUp && !bRecording)
		{
			mode = static_cast<Modes>(mode - 1);
			if (mode < FULLHD_25_MIC)
				mode = POWEROFF;
			std::cout << (int)mode;
		}
		if (bDown && !bRecording)
		{
			mode = static_cast<Modes>(mode + 1);
			if (mode > POWEROFF)
				mode = FULLHD_25_MIC;
			std::cout << (int)mode;
		}

		OLED.OLEDclearBuffer();
		OLED.setCursor(0, 0);
		if (!bRecording)
		{
			OLED.println(mode == 0 ? strModes[POWEROFF] : strModes[mode - 1]);
			OLED.setTextColor(BLACK, WHITE);
			OLED.println(strModes[mode]);
			OLED.setTextColor(WHITE, BLACK);
			OLED.println(mode == POWEROFF ? strModes[FULLHD_25_MIC] : strModes[mode + 1]);
		}
		else
		{
			auto now = std::chrono::high_resolution_clock::now();
			std::chrono::duration<double> duration = now - start;
			int seconds = duration.count();
			OLED.println(FormatTime(seconds));
			OLED.setTextColor(BLACK, WHITE);
			OLED.println(" STOP      ");
			OLED.setTextColor(WHITE, BLACK);
			if (rpicam)
				OLED.print("RPICAM ");
			if (ffmpeg)
				OLED.print("FFMPEG ");
			OLED.print('\n');
		}
		OLED.println(info);
		OLED.OLEDupdate();
		lastUp = newUp, lastOk = newOk, lastDown = newDown;
		bcm2835_delay(100);
	}
}

bool Setup()
{
	const uint16_t I2C_Speed = BCM2835_I2C_CLOCK_DIVIDER_626; //  bcm2835I2CClockDivider enum , see readme.
	const uint8_t I2C_Address = 0x3C;
	bool I2C_debug = false;

	// Check if Bcm28235 lib installed and print version.
	if (!bcm2835_init())
	{
		printf("Error 1201: init bcm2835 library , Is it installed ?\r\n");
		return false;
	}

	// Turn on I2C bus (optionally it may already be on)
	if (!OLED.OLED_I2C_ON())
	{
		printf("Error 1202: bcm2835_i2c_begin :Cannot start I2C, Running as root?\n");
		bcm2835_close(); // Close the library
		return false;
	}

	// printf("SSD1306 library Version Number :: %u\r\n",myOLED.getLibVerNum());
	// printf("bcm2835 library Version Number :: %u\r\n",bcm2835_version());
	OLED.OLEDbegin(I2C_Speed, I2C_Address, I2C_debug); // initialize the OLED

	// Set GPIO pin mode to input
	bcm2835_gpio_fsel(BUTTON_UP, BCM2835_GPIO_FSEL_INPT);
	bcm2835_gpio_fsel(BUTTON_OK, BCM2835_GPIO_FSEL_INPT);
	bcm2835_gpio_fsel(BUTTON_DOWN, BCM2835_GPIO_FSEL_INPT);

	bcm2835_gpio_set_pud(BUTTON_UP, BCM2835_GPIO_PUD_UP);
	bcm2835_gpio_set_pud(BUTTON_OK, BCM2835_GPIO_PUD_UP);
	bcm2835_gpio_set_pud(BUTTON_DOWN, BCM2835_GPIO_PUD_UP);
	if (!OLED.OLEDSetBufferPtr(OLEDwidth, OLEDheight, screenBuffer, sizeof(screenBuffer)))
		return false;
	OLED.OLEDclearBuffer();
	OLED.setTextColor(WHITE);
	OLED.setTextSize(2);
	OLED.setTextWrap(false);
	OLED.setFontNum(OLEDFont_Tiny);
	return true;
}

void End()
{
	OLED.OLEDPowerDown(); // Switch off display
	OLED.OLED_I2C_OFF();  // Switch off I2C , optional may effect other programs & devices
	bcm2835_close();	  // Close the library
}
std::string getCurrentTime()
{
	// Get current time
	std::time_t currentTime = std::time(nullptr);

	// Convert current time to local time
	std::tm *localTime = std::localtime(&currentTime);

	// Adjust for timezone
	localTime->tm_hour += 1; // Add 1 hour

	// Handle cases where the hour goes beyond 24
	if (localTime->tm_hour >= 24)
	{
		localTime->tm_hour -= 24;
		localTime->tm_mday += 1; // Increment day
	}

	// Format time into string
	std::ostringstream oss;
	oss << std::setfill('0') << std::setw(2) << localTime->tm_mday << "-"
		<< std::setfill('0') << std::setw(2) << localTime->tm_mon + 1 << "-"
		<< localTime->tm_year + 1900 << "_"
		<< std::setfill('0') << std::setw(2) << localTime->tm_hour << "-"
		<< std::setfill('0') << std::setw(2) << localTime->tm_min << "-"
		<< std::setfill('0') << std::setw(2) << localTime->tm_sec;

	return oss.str();
}

/*
	libcamera-vid -o test.h264 --width 1920 --height 1080 -t 0 --framerate 30
mkvmerge -o test.mkv test.h264

libcamera-vid --autofocus-mode continuous --inline 1 --brightness 0.1 \
--contrast 1.0 --denoise cdn_off --sharpness 1.0 --level 4.1 --framerate 25 --width 640 --height 360 \
-t 0 -n --codec libav --libav-format mpegts --libav-video-codec h264_v4l2m2m -o - \
| ffmpeg -fflags +genpts+nobuffer+igndts+discardcorrupt -flags low_delay -avioflags direct \
-hwaccel drm -hwaccel_output_format drm_prime -hide_banner \
-f alsa -thread_queue_size 8 -i plughw:0 -re \
-i - -c:v h264_v4l2m2m -b:v 1700k -fpsmax 15 \
-c:a libopus -b:a 32k -application lowdelay -ar 48000 -f s16le -threads 4 \
-f rtsp -rtsp_transport tcp rtsp://localhost:8554/mystream
*/
