#define _GNU_SOURCE

// Connect to ESP and monitor.
// Keep reconnecting
// Exit and abort when we find it is waiting for code (i.e. invalid header: 0xffffffff)
// Output what it sends otherwise

#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

int
main (int argc, const char *argv[])
{
   if (argc <= 1)
      errx (1, "Specify port");

   while (1)
   {
      int fd = open (argv[1], O_RDWR | O_NOCTTY | O_NDELAY);
      if (fd < 0)
      {
         putchar ('.');
         fflush (stdout);
         sleep (1);
         continue;
      }
      fcntl (fd, F_SETFL, 0);

      struct termios t;
      tcgetattr (fd, &t);
      cfmakeraw (&t);
      //cfsetispeed (&t, B460800);
      //cfsetospeed (&t, B460800);
      t.c_cflag &= ~HUPCL;      // disable hangup logic
      t.c_cflag &= ~CRTSCTS;    // disable hardware flow control
      t.c_cflag |= CLOCAL | CREAD;      // ignore modem controls
      t.c_iflag &= ~(IXON | IXOFF | IXANY);     //disable software flow control
      tcsetattr (fd, TCSANOW, &t);

      // RTS: EN
      // DTR: GPIO0

      int status = 0;
      ioctl (fd, TIOCMSET, &status);
      usleep (100000);
      status |= TIOCM_RTS;      // RTS (low)
      ioctl (fd, TIOCMSET, &status);
      usleep (100000);
      status &= ~TIOCM_RTS;     // RTS (high)
      ioctl (fd, TIOCMSET, &status);

      char line[1024];
      while (1)
      {
         ssize_t l = read (fd, line, sizeof (line) - 1);
         if (l <= 0)
            break;
         line[l] = 0;
         if (strstr (line, "invalid header: 0xffffffff"))
         {
            printf ("Empty chip, ready to flash\n");
            return 0;
         }
         printf ("%s", line);
      }
      close (fd);
   }
   return 0;
}
