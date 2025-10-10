#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

int main() {
    int fb_fd;
    struct fb_var_screeninfo vinfo;
    long screensize;
    long line_length;
    uint8_t *fbp;
    
    // Open the framebuffer device
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd == -1) {
        perror("Error opening framebuffer device");
        return 1;
    }
    
    // Get variable screen information
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        perror("Error reading variable screen info");
        close(fb_fd);
        return 1;
    }
    
    // Print screen information
    printf("Screen info:\n");
    printf("  Resolution: %dx%d\n", vinfo.xres, vinfo.yres);
    printf("  Virtual resolution: %dx%d\n", vinfo.xres_virtual, vinfo.yres_virtual);
    printf("  Bits per pixel: %d\n", vinfo.bits_per_pixel);
    printf("  Red: offset=%d, length=%d\n", vinfo.red.offset, vinfo.red.length);
    printf("  Green: offset=%d, length=%d\n", vinfo.green.offset, vinfo.green.length);
    printf("  Blue: offset=%d, length=%d\n", vinfo.blue.offset, vinfo.blue.length);
    
    // Calculate line length and screen size in bytes
    line_length = vinfo.xres_virtual * vinfo.bits_per_pixel / 8;
    screensize = vinfo.yres_virtual * line_length;
    
    printf("  Line length: %ld bytes\n", line_length);
    printf("  Screen size: %ld bytes\n", screensize);
    
    // Memory map the framebuffer
    fbp = (uint8_t *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fbp == MAP_FAILED) {
        perror("Error mapping framebuffer to memory");
        close(fb_fd);
        return 1;
    }
    
    // Clear screen to white
    memset(fbp, 0xFF, screensize);
    
    printf("\nWhite screen displayed! Press Enter to exit...\n");
    getchar();
    
    // Cleanup
    munmap(fbp, screensize);
    close(fb_fd);
    
    return 0;
}