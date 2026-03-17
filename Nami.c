#include <linux/uinput.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <stdbool.h>

int shift = 0;
int shiftVal = 58;

int speedW = -10, speedX = 10, speedA = -10, speedD = 10;
char holding1 = 0, holding2 = 0;
bool isPressing = false;

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

void report(int ufd, char direction)
{
    if (direction == 'W') emit(ufd, EV_REL, REL_Y, speedW);
    else if (direction == 'X') emit(ufd, EV_REL, REL_Y, speedX);
    else if (direction == 'A') emit(ufd, EV_REL, REL_X, speedA);
    else if (direction == 'D') emit(ufd, EV_REL, REL_X, speedD);
    emit(ufd, EV_SYN, SYN_REPORT, 0);
}

void incrSpeed(char direction)
{
    switch(direction)
    {
    case 'W':
        speedW--;
        break;
    case 'X':
        speedX++;
        break;
    case 'A':
        speedA--;
        break;
    case 'D':
        speedD++;
        break;
    }
}

void resetSpeed()
{
    speedW = -10;
    speedX = 10;
    speedA = -10;
    speedD = 10;
}

void setHolding(char direction)
{
    if (holding1 != direction)
    {
        holding2 = holding1;
        holding1 = direction;
    }
}

void resetHolding(char direction)
{
    if (holding1 == direction)
    {
        holding1 = holding2;
        holding2 = 0;
    }
    else if (holding2 == direction)
    {
        holding2 = 0;
    }
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
    strcpy(usetup.name, "Nami-Mouse");
    ioctl(ufd, UI_DEV_SETUP, &usetup);
    ioctl(ufd, UI_DEV_CREATE);

    int kfd = open("/dev/input/event3", O_RDONLY);
    if (kfd < 0)
    {
        perror("Could not open kfd");
        return 1;
    }

    sleep(1);
    ioctl(kfd, EVIOCGRAB, 1);

    struct input_event ev;
    while(read(kfd, &ev, sizeof(ev)) > 0)
    {
        if(ev.type != EV_KEY) continue;

        if(ev.code == shiftVal)
        {
            shift = (ev.value > 0);
            if(!shift)
            {
                resetSpeed();
                holding1 = holding2 = 0;
                if(isPressing)
                {
                    emit(ufd, EV_KEY, BTN_LEFT, 0);
                    emit(ufd, EV_SYN, SYN_REPORT, 0);
                    isPressing = false;
                }
            }
            continue;
        }

        if(shift)
        {
            if(ev.value == 0)
            {
                switch(ev.code)
                {
                case KEY_W:
                    resetHolding('W');
                    break;
                case KEY_X:
                    resetHolding('X');
                    break;
                case KEY_A:
                    resetHolding('A');
                    break;
                case KEY_D:
                    resetHolding('D');
                    break;
                case KEY_J:
                    emit(ufd, EV_KEY, BTN_LEFT, 0);
                    emit(ufd, EV_SYN, SYN_REPORT, 0);
                    isPressing = false;
                    break;
                }
                resetSpeed();
            }
            else
            {
                if (ev.value == 1)
                {
                    switch(ev.code)
                    {
                    case KEY_W:
                        setHolding('W');
                        break;
                    case KEY_X:
                        setHolding('X');
                        break;
                    case KEY_A:
                        setHolding('A');
                        break;
                    case KEY_D:
                        setHolding('D');
                        break;
                    case KEY_J:
                        emit(ufd, EV_KEY, BTN_LEFT, 1);
                        emit(ufd, EV_SYN, SYN_REPORT, 0);
                        isPressing = true;
                        break;
                    }
                }

                if (holding1 != 0)
                {
                    report(ufd, holding1);
                    incrSpeed(holding1);
                }
                if (holding2 != 0)
                {
                    report(ufd, holding2);
                    incrSpeed(holding2);
                }
            }
        }
        else
        {
            forward(ufd, &ev);
        }
    }

    ioctl(kfd, EVIOCGRAB, 0);
    ioctl(ufd, UI_DEV_DESTROY);
    close(ufd);
    close(kfd);
    return 0;
}
