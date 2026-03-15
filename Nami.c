#include <linux/uinput.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>

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
    struct input_event ev;

    int shift = 0;
    int ctrl  = 0;
    int alt   = 0;

    while(1)
    {
        read(kfd, &ev, sizeof(ev));
        if(ev.type != EV_KEY)
            continue;

        /* --- Track Ctrl --- */
        if(ev.code == KEY_LEFTCTRL || ev.code == KEY_RIGHTCTRL)
        {
            ctrl = (ev.value != 0);
            if(ctrl && shift)
            {
                shift = 0;
                emit(ufd, EV_KEY, KEY_LEFTSHIFT, 0);
                emit(ufd, EV_SYN, SYN_REPORT, 0);
                ioctl(kfd, EVIOCGRAB, 0);
            }
            forward(ufd, &ev);
            continue;
        }

        /* --- Track Alt --- */
        if(ev.code == KEY_LEFTALT || ev.code == KEY_RIGHTALT)
        {
            alt = (ev.value != 0);
            if(alt && shift)
            {
                shift = 0;
                emit(ufd, EV_KEY, KEY_LEFTSHIFT, 0);
                emit(ufd, EV_SYN, SYN_REPORT, 0);
                ioctl(kfd, EVIOCGRAB, 0);
            }
            forward(ufd, &ev);
            continue;
        }

        /* --- Shift handling --- */
        if(ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT)
        {
            if(ev.value == 1 && !shift && !ctrl && !alt)
            {
                shift = 1;
                ioctl(kfd, EVIOCGRAB, 1);
            }
            else if(ev.value == 0 && shift)
            {
                shift = 0;
                emit(ufd, EV_KEY, ev.code, 0);
                emit(ufd, EV_SYN, SYN_REPORT, 0);
                ioctl(kfd, EVIOCGRAB, 0);
            }
            else
            {
                forward(ufd, &ev);
            }
            continue;
        }

        if(ctrl || alt)
        {
            forward(ufd, &ev);
            continue;
        }

        if(ev.value != 1)
            continue;

        if(shift)
        {
            switch(ev.code)
            {
                case KEY_W:
                    emit(ufd, EV_REL, REL_Y, -10);
                    emit(ufd, EV_SYN, SYN_REPORT, 0);
                    break;
                case KEY_S:
                    emit(ufd, EV_REL, REL_Y, 10);
                    emit(ufd, EV_SYN, SYN_REPORT, 0);
                    break;
                case KEY_A:
                    emit(ufd, EV_REL, REL_X, -10);
                    emit(ufd, EV_SYN, SYN_REPORT, 0);
                    break;
                case KEY_D:
                    emit(ufd, EV_REL, REL_X, 10);
                    emit(ufd, EV_SYN, SYN_REPORT, 0);
                    break;
                case KEY_J:
                    emit(ufd, EV_KEY, BTN_LEFT, 1);
                    emit(ufd, EV_SYN, SYN_REPORT, 0);
                    emit(ufd, EV_KEY, BTN_LEFT, 0);
                    emit(ufd, EV_SYN, SYN_REPORT, 0);
                    break;
                default:
                    forward(ufd, &ev);
                    break;
            }
        }
        else
        {
            printf("letter: %d\n", ev.code);
        }
    }
}
