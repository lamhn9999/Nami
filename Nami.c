#include <linux/uinput.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>

int shift = 0;
int shiftVal = 58;

int speedW = -10;
int speedS = 10;
int speedA = -10;
int speedD = 10;

void emit(int fd, int type, int code, int val)
{
    struct input_event ie;
    memset(&ie, 0, sizeof(ie));
    gettimeofday(&ie.time, NULL);
    ie.type = type;
    ie.code = code;
    ie.value = val;
    write(fd, &ie, sizeof(ie));
}

void forward(int ufd, struct input_event *ev)
{
    emit(ufd, ev->type, ev->code, ev->value);
    emit(ufd, EV_SYN, SYN_REPORT, 0);
}

void resetSpeed()
{
    speedW = -10;
    speedS = 10;
    speedA = -10;
    speedD = 10;
}

int main()
{
    int ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    ioctl(ufd, UI_SET_EVBIT, EV_KEY);
    ioctl(ufd, UI_SET_KEYBIT, BTN_LEFT);
    for(int i = 0; i < KEY_MAX; i++)
        ioctl(ufd, UI_SET_KEYBIT, i);
    ioctl(ufd, UI_SET_EVBIT, EV_REL);
    ioctl(ufd, UI_SET_RELBIT, REL_X);
    ioctl(ufd, UI_SET_RELBIT, REL_Y);

    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    strcpy(usetup.name, "keyboard-mouse");
    ioctl(ufd, UI_DEV_SETUP, &usetup);
    ioctl(ufd, UI_DEV_CREATE);
    sleep(1);

    int kfd = open("/dev/input/event3", O_RDONLY);
    ioctl(kfd, EVIOCGRAB, 1);

    struct input_event ev;
    while(1)
    {
        read(kfd, &ev, sizeof(ev));
        if(ev.type != EV_KEY)
            continue;

        /* --- Window button handling --- */
        if(ev.code == shiftVal)
        {
            shift = (ev.value > 0);
            if(!shift) resetSpeed();
            continue;
        }

        if(shift)
        {
            if(ev.value == 0)
            {
                resetSpeed();
            }
            else switch(ev.code)
            {
                case KEY_W:
                    emit(ufd, EV_REL, REL_Y, speedW--);
                    emit(ufd, EV_SYN, SYN_REPORT, 0);
                    break;
                case KEY_S:
                    emit(ufd, EV_REL, REL_Y, speedS++);
                    emit(ufd, EV_SYN, SYN_REPORT, 0);
                    break;
                case KEY_A:
                    emit(ufd, EV_REL, REL_X, speedA--);
                    emit(ufd, EV_SYN, SYN_REPORT, 0);
                    break;
                case KEY_D:
                    emit(ufd, EV_REL, REL_X, speedD++);
                    emit(ufd, EV_SYN, SYN_REPORT, 0);
                    break;
                case KEY_J:
                    emit(ufd, EV_KEY, BTN_LEFT, 1);
                    emit(ufd, EV_SYN, SYN_REPORT, 0);
                    emit(ufd, EV_KEY, BTN_LEFT, 0);
                    emit(ufd, EV_SYN, SYN_REPORT, 0);
                    break;
                default:
                    break;
            }
        }
        else
        {
            forward(ufd, &ev);
        }
    }
}
