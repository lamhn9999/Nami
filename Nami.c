#include <linux/uinput.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <stdbool.h>
#include <dirent.h>
#include <stdint.h>

/** Configuration **/
#define SHIFT_KEY KEY_CAPSLOCK
#define CLICK_KEY KEY_J
#define BASE_SPEED 10
#define POLL_DELAY 8000 // 8ms for smooth movement

typedef struct
{
    int code;
    char label;
    int *speed;
    int axis; // REL_X or REL_Y
} KeyMap;

/** Global State **/
int speedW = -BASE_SPEED, speedX = BASE_SPEED, speedA = -BASE_SPEED, speedD = BASE_SPEED;
char holding[2] = {0, 0};
bool is_clicking = false;

// Map keys to their behavior
KeyMap movement_keys[] =
{
    {KEY_W, 'W', &speedW, REL_Y},
    {KEY_X, 'X', &speedX, REL_Y},
    {KEY_A, 'A', &speedA, REL_X},
    {KEY_D, 'D', &speedD, REL_X}
};
const int KEY_COUNT = 4;

/** Core Logic **/
void emit(int fd, int type, int code, int val)
{
    struct input_event ie = {0};
    gettimeofday(&ie.time, NULL);
    ie.type = type;
    ie.code = code;
    ie.value = val;
    write(fd, &ie, sizeof(ie));
}

void sync_report(int fd)
{
    emit(fd, EV_SYN, SYN_REPORT, 0);
}

void reset_state()
{
    speedW = -BASE_SPEED;
    speedX = BASE_SPEED;
    speedA = -BASE_SPEED;
    speedD = BASE_SPEED;
    holding[0] = holding[1] = 0;
}

void update_holding(char label, bool pressed)
{
    if (pressed)
    {
        if (holding[0] != label)
        {
            holding[1] = holding[0];
            holding[0] = label;
        }
    }
    else
    {
        if (holding[0] == label) holding[0] = holding[1];
        holding[1] = 0;
    }
}

void perform_movement(int ufd)
{
    for (int i = 0; i < 2; i++)
    {
        if (holding[i] == 0) continue;

        for (int j = 0; j < KEY_COUNT; j++)
        {
            if (movement_keys[j].label == holding[i])
            {
                emit(ufd, EV_REL, movement_keys[j].axis, *movement_keys[j].speed);
                // Increment speed (absolute value)
                if (*movement_keys[j].speed > 0) (*movement_keys[j].speed)++;
                else (*movement_keys[j].speed)--;
            }
        }
    }
    sync_report(ufd);
}

// Returns an open fd to the first USB dongle keyboard found, or -1 if none.
int find_dongle_keyboard()
{
    DIR *dir = opendir("/dev/input");
    if (!dir) { perror("opendir /dev/input"); return -1; }

    int result = -1;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL && result < 0)
    {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;

        char path[64];
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        // Skip own virtual device
        char name[256] = "";
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        if (strcmp(name, "Nami-Pro-Mouse") == 0) { close(fd); continue; }

        // Must be USB (dongle receiver shows as BUS_USB)
        struct input_id id = {0};
        if (ioctl(fd, EVIOCGID, &id) < 0 || id.bustype != BUS_USB)
        {
            close(fd);
            continue;
        }

        // Must support EV_KEY
        uint8_t evbits[(EV_MAX + 7) / 8] = {0};
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0 ||
            !(evbits[EV_KEY / 8] & (1 << (EV_KEY % 8))))
        {
            close(fd);
            continue;
        }

        // Must have letter keys (rules out mice/joysticks on USB)
        uint8_t keybits[(KEY_MAX + 7) / 8] = {0};
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) < 0 ||
            !(keybits[KEY_A / 8] & (1 << (KEY_A % 8))))
        {
            close(fd);
            continue;
        }

        printf("Dongle keyboard: %s (%s) vendor=%04x product=%04x\n",
               path, name, id.vendor, id.product);
        result = fd;
    }

    closedir(dir);
    if (result < 0) fprintf(stderr, "No dongle keyboard found\n");
    return result;
}

int main()
{
    int ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    // int kfd = open("/dev/input/event3", O_RDONLY); // Note: verify your event ID
    int kfd = find_dongle_keyboard();

    if (ufd < 0 || kfd < 0) return perror("Open failed"), 1;

    // Setup uinput device
    ioctl(ufd, UI_SET_EVBIT, EV_KEY);
    ioctl(ufd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(ufd, UI_SET_EVBIT, EV_REL);
    ioctl(ufd, UI_SET_RELBIT, REL_X);
    ioctl(ufd, UI_SET_RELBIT, REL_Y);
    for (int i = 0; i < KEY_MAX; i++) ioctl(ufd, UI_SET_KEYBIT, i);

    struct uinput_setup usetup = {0};
    usetup.id.bustype = BUS_USB;
    strcpy(usetup.name, "Nami-Pro-Mouse");
    ioctl(ufd, UI_DEV_SETUP, &usetup);
    ioctl(ufd, UI_DEV_CREATE);

    sleep(1);
    ioctl(kfd, EVIOCGRAB, 1);

    struct input_event ev;
    bool shift_active = false;

    while (read(kfd, &ev, sizeof(ev)) > 0)
    {
        if (ev.type != EV_KEY)
        {
            continue;
        }

        // Toggle Shift Mode
        if (ev.code == SHIFT_KEY)
        {
            shift_active = (ev.value > 0);
            if (!shift_active)
            {
                if (is_clicking) emit(ufd, EV_KEY, BTN_LEFT, 0);
                reset_state();
                sync_report(ufd);
            }
            continue;
        }

        if (shift_active)
        {
            bool is_pressed = (ev.value > 0);
            bool handled = false;

            // Handle Movement Keys
            for (int i = 0; i < KEY_COUNT; i++)
            {
                if (ev.code == movement_keys[i].code)
                {
                    update_holding(movement_keys[i].label, is_pressed);
                    if (!is_pressed) reset_state();
                    handled = true;
                    break;
                }
            }

            // Handle Clicking
            if (ev.code == CLICK_KEY)
            {
                is_clicking = is_pressed;
                emit(ufd, EV_KEY, BTN_LEFT, is_pressed ? 1 : 0);
                sync_report(ufd);
                handled = true;
            }

            // If a key is held, execute movement and ramp speed
            if (holding[0] != 0)
            {
                perform_movement(ufd);
                usleep(POLL_DELAY); // Prevents CPU max-out and smooths ramp
            }
        }
        else
        {
            // Passthrough mode
            emit(ufd, ev.type, ev.code, ev.value);
            sync_report(ufd);
        }
    }

    ioctl(kfd, EVIOCGRAB, 0);
    ioctl(ufd, UI_DEV_DESTROY);
    return 0;
}
