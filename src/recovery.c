#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_ttf.h>
#include "font.h"
#include "background.h"
#include "opkscan.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <linux/fb.h>
#include <linux/fs.h>
#include <ctime>
#include <sys/time.h>   /* for settimeofday() */

#ifndef TARGET_RETROFW
	#define system(x) printf(x); printf("\n")
#endif

#ifndef TARGET_RETROFW
	#define DBG(x) printf("%s:%d %s %s\n", __FILE__, __LINE__, __func__, x);
#else
	#define DBG(x)
#endif

#define WIDTH  320
#define HEIGHT 240

#define GPIO_BASE		0x10010000
#define PAPIN			((0x10010000 - GPIO_BASE) >> 2)
#define PBPIN			((0x10010100 - GPIO_BASE) >> 2)
#define PCPIN			((0x10010200 - GPIO_BASE) >> 2)
#define PDPIN			((0x10010300 - GPIO_BASE) >> 2)
#define PEPIN			((0x10010400 - GPIO_BASE) >> 2)
#define PFPIN			((0x10010500 - GPIO_BASE) >> 2)

#define BTN_X			SDLK_SPACE
#define BTN_A			SDLK_LCTRL
#define BTN_B			SDLK_LALT
#define BTN_Y			SDLK_LSHIFT
#define BTN_L			SDLK_TAB
#define BTN_R			SDLK_BACKSPACE
#define BTN_START		SDLK_RETURN
#define BTN_SELECT		SDLK_ESCAPE
#define BTN_BACKLIGHT	SDLK_3
#define BTN_POWER		SDLK_END
#define BTN_UP			SDLK_UP
#define BTN_DOWN		SDLK_DOWN
#define BTN_LEFT		SDLK_LEFT
#define BTN_RIGHT		SDLK_RIGHT

TTF_Font *font = NULL;
SDL_Surface *screen = NULL;
// SDL_Surface *StretchSurface = NULL;
SDL_Surface *bg = NULL;
SDL_Event event;

uint8_t *keys;

SDL_Color txtColor = {200, 200, 220};
SDL_Color titleColor = {200, 200, 0};
SDL_Color subTitleColor = {0, 200, 0};
SDL_Color powerColor = {200, 0, 0};

static char buf[1024];
uint8_t nextline = 24;

int memdev = -1;
uint32_t *mem;

enum modes {
	MODE_UNKNOWN,
	MODE_UDC,
	MODE_NETWORK,
	MODE_RESIZE,
	MODE_FSCK,
	MODE_DEFL,
	MODE_START,
	MODE_MENU
};

struct callback_map_t {
  const char *text;
  void (*callback)(void);
};

uint8_t file_exists(const char path[512]) {
	struct stat s;
	return !!(stat(path, &s) == 0 && (s.st_mode & S_IFREG || s.st_mode & S_IFBLK)); // exists and is file or block
}

void quit(int err) {
	DBG("");
	system("sync");
	font = NULL;
	SDL_Quit();
	TTF_Quit();
	exit(err);
}

void reboot() {
	system("sync; reboot -f");
	quit(0);
}

void poweroff() {
	system("sync; poweroff -f");
	quit(0);
}

int draw_text(int x, int y, const char buf[64], SDL_Color txtColor) {
	if (!strcmp(buf, "")) return y;
	DBG("");
	SDL_Surface *msg = TTF_RenderText_Blended(font, buf, txtColor);
	SDL_Rect rect;
	rect.x = x;
	rect.y = y;
	rect.w = msg->w;
	rect.h = msg->h;
	SDL_BlitSurface(msg, NULL, screen, &rect);
	SDL_FreeSurface(msg);
	return y + msg->h + 2;
}

int draw_screen(const char title[64], const char footer[64]) {
	DBG("");
	SDL_Rect rect;
	rect.w = WIDTH;
	rect.h = HEIGHT;
	rect.x = 0;
	rect.y = 0;

	SDL_BlitSurface(bg, NULL, screen, NULL);

	// title
	draw_text(247, 4, "RetroFW", titleColor);
	draw_text(10, 4, title, titleColor);

	rect.w = WIDTH - 20;
	rect.h = 1;
	rect.x = 10;
	rect.y = 20;
	SDL_FillRect(screen, &rect,  SDL_MapRGB(screen->format, 200, 200, 200));

	rect.y = HEIGHT - 20;
	SDL_FillRect(screen, &rect,  SDL_MapRGB(screen->format, 200, 200, 200));

	draw_text(10, 222, footer, powerColor);

	return 32;
}

int check_part() {
	DBG("");
	if (file_exists("/boot/.prsz")) return MODE_RESIZE;
	if (file_exists("/boot/.defl")) return MODE_DEFL;
	if (file_exists("/boot/.fsck")) return MODE_FSCK;
	return MODE_UNKNOWN;
}

void fsck() {
	nextline = draw_screen("FILE SYSTEM CHECK", "");
	nextline = draw_text(10, nextline, "Checking file system", txtColor);
	nextline = draw_text(10, nextline, "This may take several minutes", txtColor);
	nextline = draw_text(10, nextline, "Please wait...", txtColor);
	SDL_Flip(screen);

	DBG("");

	if (file_exists("/boot/.fsck")) {
		// first boot. remove fsck flag
		system("mount -o remount,rw /boot; rm '/boot/.fsck'; mount -o remount,ro /boot");
	} else {
		// check external fs only after first boot (manual trigger)
		system("fsck.vfat -va $(ls /dev/mmcblk1* | tail -n 1)");
	}

	system("umount -fl /dev/mmcblk0p2 &> /dev/null");
	system("fsck.vfat -va /dev/mmcblk0p2");
	// system("fsck.vfat -va $(ls /dev/mmcblk0* | tail -n 1)");
	// system("fsck.vfat -va $(ls /dev/mmcblk1* | tail -n 1)");

	nextline = draw_text(10, nextline, "Done. Rebooting...", txtColor);
	SDL_Flip(screen);

	reboot();
}

void fatsize(char *size) {
#ifndef TARGET_RETROFW
	sprintf(size, "0.0 GiB");
	return;
#endif

	sprintf(size, "N/A");

	int fd = open("/dev/mmcblk0p3", O_RDONLY);
	uint64_t bsize = 0;
	ioctl(fd, BLKGETSIZE, &bsize);
	close(fd);

	uint32_t totalMiB = (bsize * 512) / (1024 * 1024);

	if (totalMiB >= 1024)
		sprintf(size, "%d.%d GiB", (totalMiB / 1024), ((totalMiB % 1024) * 10) / 1024);
	else
		sprintf(size, "%d MiB", totalMiB);
}

void fatresize() {
	DBG("");
	nextline = draw_screen("PARTITION MANAGER", "");
	nextline = draw_text(10, nextline, "Updating partition table", txtColor);
	nextline = draw_text(10, nextline, "This may take several minutes", txtColor);
	nextline = draw_text(10, nextline, "Please wait...", txtColor);

	SDL_Flip(screen);

#ifdef TARGET_RETROFW
	system("mount -o remount,rw /boot; rm '/boot/.prsz'; mount -o remount,ro /boot");
	system("sync; umount -fl /home/retrofw $(ls --color=never /dev/mmcblk0* | tail -n 1) /dev/mmcblk1* &> /dev/null");
	system("echo \"start= 342016, type=c\" | sfdisk --append --no-reread /dev/mmcblk0");

	system("sync");
#endif

	nextline = draw_text(10, nextline, "Done. Rebooting...", txtColor);

	SDL_Flip(screen);

	SDL_Delay(1e3);

	reboot();
}

void udc() {
	DBG("");

	nextline = draw_screen("USB MODE", "SELECT: EXIT");
	nextline = draw_text(10, nextline, "- Mount the device to copy files", txtColor);
	nextline = draw_text(10, nextline, "- Safely remove the USB drive", txtColor);
	nextline = draw_text(10, nextline, "- Disconnect the USB cable", txtColor);

	SDL_Flip(screen);

	system("rmmod g_ether; rmmod g_file_storage");
	system("modprobe g_file_storage");
	system("echo \"\" > /sys/devices/platform/musb_hdrc.0/gadget/gadget-lun1/file; echo \"$(ls --color=never /dev/mmcblk0* | tail -n 1)\" > /sys/devices/platform/musb_hdrc.0/gadget/gadget-lun1/file");
	system("echo \"\" > /sys/devices/platform/musb_hdrc.0/gadget/gadget-lun0/file; echo \"$(ls --color=never /dev/mmcblk1* | tail -n 1)\" > /sys/devices/platform/musb_hdrc.0/gadget/gadget-lun0/file");

	while (1) {
		if (SDL_WaitEvent(&event) && event.type == SDL_KEYDOWN && event.key.keysym.sym == BTN_SELECT) { // SELECT
			break;
		}
	}
}

void network() {
	DBG("");
	nextline = draw_screen("NETWORK MODE", "SELECT: EXIT");
	nextline = draw_text(10, nextline, "- Set up the network in your PC", txtColor);
	nextline = draw_text(10, nextline, "- FTP or Telnet to 169.254.1.1", txtColor);
	nextline = draw_text(10, nextline, "- Transfer files/run commands", txtColor);
	SDL_Flip(screen);

	system("rmmod g_file_storage; modprobe g_ether; ifdown usb0; ifup usb0");

	while (1) {
		if (SDL_WaitEvent(&event) && event.type == SDL_KEYDOWN && event.key.keysym.sym == BTN_SELECT) { // SELECT
			break;
		}
	}
}

void format_ext() {
	nextline = draw_screen("FORMAT EXT SD", "SELECT + Y: CONFIRM     B: CANCEL");
	nextline = draw_text(10, nextline, "WARNING", powerColor);
	nextline = draw_text(10, nextline, "This will format the external", txtColor);
	nextline = draw_text(10, nextline, "SD card and all files will", txtColor);
	nextline = draw_text(10, nextline, "be deleted", txtColor);
	nextline = draw_text(10, nextline, " ", txtColor);
	nextline = draw_text(10, nextline, "THIS CAN'T BE UNDONE", powerColor);
	SDL_Flip(screen);

	while (SDL_WaitEvent(&event)) {
		if (event.type != SDL_KEYDOWN) continue;

		if (keys[BTN_SELECT] && keys[BTN_Y]) {
			nextline = draw_screen("FORMAT EXT SD", "");
			nextline = draw_text(10, nextline, "Formatting external SD card", txtColor);
			nextline = draw_text(10, nextline, "This may take several minutes", txtColor);
			nextline = draw_text(10, nextline, "Please wait...", txtColor);
			SDL_Flip(screen);

			system("sync; umount -fl /dev/mmcblk1* &> /dev/null");
			system("(echo o; echo n; echo p; echo 1; echo  ; echo  ; echo w; ) | fdisk /dev/mmcblk1");
			// system("echo 'start=2048, type=83' | sfdisk /dev/mmcblk1");

			system("sync; partprobe; mdev -s");
			system("mkfs.vfat -F32 -va -n 'RetroFW_SD' /dev/mmcblk1p1");
			system("mdev -s; mount -a");

			nextline = draw_text(10, nextline, "Done.", txtColor);
			SDL_Flip(screen);

			break;
		} else if (keys[BTN_B]) {
			break;
		}
	}
}

void format_int() {
	nextline = draw_screen("DATA RESET", "");
	nextline = draw_text(10, nextline, "Restoring default data", txtColor);
	nextline = draw_text(10, nextline, "This may take several minutes", txtColor);
	nextline = draw_text(10, nextline, "Please wait...", txtColor);
	SDL_Flip(screen);

	system("sync; umount -fl /dev/mmcblk0p2 &> /dev/null");
	system("mkfs.vfat -F32 -va -n 'RETROFW' /dev/mmcblk0p2");
	system("mount -a");
	system("gunzip -c /home/.retrofw.tar.gz | tar -C /home/retrofw/ -x");
	system("mount -o remount,rw /boot; rm '/boot/.defl'; mount -o remount,ro /boot");

	fsck();
}

void data_reset() {
	DBG("");
	nextline = draw_screen("DATA RESET", "SELECT + Y: CONFIRM     B: CANCEL");
	nextline = draw_text(10, nextline, "WARNING", powerColor);
	nextline = draw_text(10, nextline, "This will format the data", txtColor);
	nextline = draw_text(10, nextline, "partition and all files will", txtColor);
	nextline = draw_text(10, nextline, "be deleted", txtColor);
	nextline = draw_text(10, nextline, " ", txtColor);
	nextline = draw_text(10, nextline, "THIS CAN'T BE UNDONE", powerColor);
	SDL_Flip(screen);

	while (SDL_WaitEvent(&event)) {
		if (event.type != SDL_KEYDOWN) continue;

		if (keys[BTN_SELECT] && keys[BTN_Y]) {
			format_int();
			break;
		} else if (keys[BTN_B]) {
			break;
		}
	}
}

void stop() {
	DBG("");
	system("echo '' > /sys/devices/platform/musb_hdrc.0/gadget/gadget-lun0/file");
	system("echo '' > /sys/devices/platform/musb_hdrc.0/gadget/gadget-lun1/file");
	system("sync");
	system("killall dnsmasq; rmmod g_ether; rmmod g_file_storage");
	system("mdev -s; mount -a");
}

void opkrun(int argc, char* argv[]) {
	snprintf(buf, sizeof(buf), "umount -fl /mnt &> /dev/null; mount -o loop \"%s\" /mnt", argv[3]);
	system(buf);

	snprintf(buf, sizeof(buf), "HOME='%s' exec /mnt/", getenv("HOME"));

	for (uint32_t i = 4; i < argc; i++) {
		strcat(buf, "\"");
		strcat(buf, argv[i]);
		strcat(buf, "\" ");
	}

	chdir("/mnt");
	execlp("/bin/sh", "/bin/sh", "-c", buf, NULL);
}

void opkscan(int argc, char* argv[]) {
#ifdef TARGET_RETROFW
	// FILE *fp = popen("gunzip | sh > /dev/null", "w");
	snprintf(buf, sizeof(buf), "gunzip | sh -s - ");
	for (uint32_t i = 3; i < argc; i++) {
		strcat(buf, "\"");
		strcat(buf, argv[i]);
		strcat(buf, "\" ");
	}

	FILE *fp = popen(buf, "w");
	if (!fp) return;
	for (uint16_t i = 0; i < sizeof(_opkscan) ; i++) fputc(_opkscan[i], fp);
	pclose(fp);
#endif
}

void free_tty() {
	int fd = open("/dev/tty0", O_RDONLY);
	if (fd > 0) {
		ioctl(fd, VT_UNLOCKSWITCH, 1);
		ioctl(fd, KDSETMODE, KD_TEXT);
		ioctl(fd, KDSKBMODE, K_XLATE);
	}
	close(fd);
}

struct callback_map_t cb_map[] = {
  { "USB Mode", udc },
  { "Network Mode", network },
  { "Check File System", fsck },
  // { "Resize File System", fatresize },
  { "Data Reset", data_reset },
  { "Format Ext SD Card", format_ext },
  { "Reboot", reboot },
  { "Power Off", poweroff },
};
unsigned int cb_size = (sizeof(cb_map) / sizeof(cb_map[0]));

void sync_date_time(time_t t) {
#if defined(TARGET_RETROFW)
	struct timeval tv = { t, 0 };
	settimeofday(&tv, NULL);
	system("hwclock --systohc &");
#endif
}

void init_date_time() {
	system("hwclock --hctosys");

	time_t now = time(0);
	const uint32_t t = __BUILDTIME__;

	if (now < t) {
		sync_date_time(t);
	}
}

int main(int argc, char* argv[]) {
	keys = SDL_GetKeyState(NULL);

	init_date_time();

	if (argc > 1 && !strcmp(argv[1], "stop")) {
		system("hwclock --systohc");
		return 0;
	}

#ifdef TARGET_RETROFW
	if (!file_exists("/dev/mmcblk1")) {
		for (int i = 4; i < cb_size - 1; i++)
			cb_map[i] = cb_map[i + 1];
		cb_size--;
	}
#endif

	DBG("");
	signal(SIGINT, &quit);
	signal(SIGSEGV,&quit);
	signal(SIGTERM,&quit);
	DBG("");

	char title[64] = "RECOVERY MODE";

#ifdef TARGET_RETROFW
	memdev = open("/dev/mem", O_RDWR);
	if (memdev > 0) {
		mem  = (uint32_t*)mmap(0, 2048, PROT_READ | PROT_WRITE, MAP_SHARED, memdev, GPIO_BASE);
		if (!(mem[PEPIN] >> 07 & 0b1))	{ keys[BTN_X]			= SDL_PRESSED; }	/* X */
		if (!(mem[PDPIN] >> 22 & 0b1))	{ keys[BTN_A]			= SDL_PRESSED; }	/* A */
		if (!(mem[PDPIN] >> 23 & 0b1))	{ keys[BTN_B]			= SDL_PRESSED; }	/* B */
		if (!(mem[PEPIN] >> 11 & 0b1))	{ keys[BTN_Y]			= SDL_PRESSED; }	/* Y */
		if (!(mem[PBPIN] >> 23 & 0b1))	{ keys[BTN_L]			= SDL_PRESSED; }	/* L */
		if (!(mem[PDPIN] >> 24 & 0b1))	{ keys[BTN_R]			= SDL_PRESSED; }	/* R */
		if ( (mem[PDPIN] >> 18 & 0b1))	{ keys[BTN_START]		= SDL_PRESSED; }	/* START */
		if ( (mem[PDPIN] >> 17 & 0b1))	{ keys[BTN_SELECT]		= SDL_PRESSED; }	/* SELECT */
		if (!(mem[PDPIN] >> 21 & 0b1))	{ keys[BTN_BACKLIGHT]	= SDL_PRESSED; }	/* BACKLIGHT */
		if (!(mem[PAPIN] >> 30 & 0b1))	{ keys[BTN_POWER]		= SDL_PRESSED; }	/* POWER */
		if (!(mem[PBPIN] >> 25 & 0b1))	{ keys[BTN_UP]			= SDL_PRESSED; }	/* UP */
		if (!(mem[PBPIN] >> 24 & 0b1))	{ keys[BTN_DOWN]		= SDL_PRESSED; }	/* DOWN */
		if (!(mem[PDPIN] >> 00 & 0b1))	{ keys[BTN_LEFT]		= SDL_PRESSED; }	/* LEFT */
		if (!(mem[PBPIN] >> 26 & 0b1))	{ keys[BTN_RIGHT]		= SDL_PRESSED; }	/* RIGHT */
	}
	munmap(mem, 2048);
	close(memdev);
#endif

	int mode = MODE_UNKNOWN;

	if (((keys[BTN_POWER] == SDL_PRESSED || keys[BTN_SELECT] == SDL_PRESSED) && keys[BTN_Y] == SDL_PRESSED) || (argc > 2 && !strcmp(argv[1], "network") && !strcmp(argv[2], "on"))) {
		// hold boot - network - no gui - for debugging
		system("rmmod g_file_storage; modprobe g_ether; ifdown usb0; ifup usb0");
		if (!(argc > 2 && !strcmp(argv[1], "network") && !strcmp(argv[2], "on"))) {
			system("modprobe fbcon");

			system("echo -e \"\e[1;36m ____      _            \e[31m _____ _     _\" > /dev/tty0");
			system("echo -e \"\e[36m|  _ \\ ___| |_ _ __ ___ \e[31m|  ___| | _ | |\" > /dev/tty0");
			system("echo -e \"\e[36m| |_) / _ \\ __| '__/ _ \\\\\e[31m| |__ | |/ \\| |\" > /dev/tty0");
			system("echo -e \"\e[36m|  _ <  __/ |_| | | '_' \e[31m|  __||  .-.  |\" > /dev/tty0");
			system("echo -e \"\e[36m|_| \\_\\___|\\__|_|  \\___/\e[31m|_|   |_/   \\_|\" > /dev/tty0");
			system("echo -e \"\e[0;37m\" > /dev/tty0");

			system("echo '- Set up the USB network in your PC' > /dev/tty0");
			system("echo '- FTP or Telnet to 169.254.1.1' > /dev/tty0");
			system("echo '- Copy the files/run shell commands' > /dev/tty0");
			system("echo '- Power off and reboot' > /dev/tty0");

			while (argc <= 2) sleep(1000000);
		}

		return 0;
	}

	mode = check_part();

	if (argc > 1 && !strcmp(argv[1], "network")) {
		sprintf(title, "NETWORK MODE");
		mode = MODE_NETWORK;
	} else if (argc > 1 && !strcmp(argv[1], "storage")) {
		sprintf(title, "USB STORAGE MODE");
		mode = MODE_UDC;
	} else if (mode == MODE_RESIZE || (argc > 1 && !strcmp(argv[1], "fatresize"))) {
		sprintf(title, "PARTITION EXTENDER");
	} else if (mode == MODE_FSCK || (argc > 1 && !strcmp(argv[1], "fsck"))) {
		sprintf(title, "FILE SYSTEM CHECK");
	} else if (mode == MODE_DEFL) {
		sprintf(title, "DATA RESET");
	} else if (((keys[BTN_POWER] == SDL_PRESSED || keys[BTN_SELECT] == SDL_PRESSED) && (keys[BTN_B] == SDL_PRESSED || keys[BTN_A] == SDL_PRESSED)) || (argc > 1 && !strcmp(argv[1], "menu"))) {
		sprintf(title, "RECOVERY MODE");
		mode = MODE_MENU;
	} else if (argc > 1 && !strcmp(argv[1], "start")) {
		mode = MODE_START;
	} else if (argc > 4 && !strcmp(argv[1], "opk") && !strcmp(argv[2], "run")) {
		opkrun(argc, argv);
	} else if (argc > 2 && !strcmp(argv[1], "opk") && (!strcmp(argv[2], "update") || !strcmp(argv[2], "install"))) {
		opkscan(argc, argv);
		quit(0); return 0;
	}

	setenv("SDL_FBCON_DONT_CLEAR", "1", 1);
	setenv("SDL_NOMOUSE", "1", 1);
	setenv("TERM", "vt100", 1);
	setenv("HOME", "/home/retrofw", 1);

	free_tty();

	if (mode == MODE_START) {
		if (file_exists("/home/retrofw/autoexec.sh")) {
		if (file_exists("/root/swap.img") || file_exists("/root/local/swap.img")) {
			system("swapon /root/swap.img /root/local/swap.img");
		}

			execlp("/bin/sh", "/bin/sh", "-c", "source /home/retrofw/autoexec.sh", NULL);
			usleep(5000000);
		} else if (file_exists("/home/retrofw/apps/gmenu2x/gmenu2x")) {
			execlp("/home/retrofw/apps/gmenu2x/gmenu2x", "/home/retrofw/apps/gmenu2x/gmenu2x", NULL);
			usleep(5000000);
		} else {
			mode = MODE_MENU;
		}
	}

	if (mode == MODE_UNKNOWN || mode == MODE_START) {
		quit(0);
		return 0;
	}

	printf("%s\n", title);

	SDL_Rect rect = {0};

	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		printf("Could not initialize SDL: %s\n", SDL_GetError());
		udc();
		return -1;
	}
	SDL_PumpEvents();
	SDL_ShowCursor(0);

	screen = SDL_SetVideoMode(WIDTH, HEIGHT, 16, SDL_SWSURFACE);

	SDL_EnableKeyRepeat(0, 0);
	SDL_Delay(50);
	SDL_PumpEvents();

	if (TTF_Init() == -1) {
		printf("TTF_Init: %s\n", SDL_GetError());
		return -1;
	}

	font = TTF_OpenFontRW(SDL_RWFromMem(rwfont, sizeof(rwfont)), 1, 12);
	TTF_SetFontHinting(font, TTF_HINTING_NORMAL);
	TTF_SetFontOutline(font, 0);

	bg = IMG_Load_RW(SDL_RWFromMem(background, sizeof(background)), 1);
	if(!bg) {
		printf("IMG_Load_RW: %s\n", SDL_GetError());
	}

	switch (mode) {
		case MODE_RESIZE:
			fatresize();
			break;
		case MODE_FSCK:
			fsck();
		  break;
		case MODE_DEFL:
			format_int();
		  break;
		case MODE_UDC:
			udc();
			break;
		case MODE_NETWORK:
			network();
			break;
		case MODE_MENU:
			goto mode_menu;
			break;
	}
	stop(); quit(0); return 0; // run for all except mode_menu

	mode_menu: /* jump */;

	int selected = 0;
	while (1) {
		nextline = draw_screen(title, "A: SELECT");

		for (int i = 0; i < cb_size; i++) {
			SDL_Color selColor = txtColor;
			if (selected == i) selColor = subTitleColor;
			nextline = draw_text(10, nextline, cb_map[i].text, selColor);
		}

		SDL_Flip(screen);

		if (SDL_WaitEvent(&event)) {
			SDL_PumpEvents();

			if (event.type == SDL_KEYDOWN) {
				if (keys[BTN_UP]) {
					selected--;
					if (selected < 0) selected = cb_size - 1;
				} else if (keys[BTN_DOWN]) {
					selected++;
					if (selected >= cb_size) selected = 0;
				} else if (keys[BTN_LEFT]) {
					selected = 0;
				} else if (keys[BTN_RIGHT]) {
					selected = cb_size - 1;
				} else if (keys[BTN_A]) {
					cb_map[selected].callback();
				}
			}
		}
	}

	reboot();

	return 0;
}
