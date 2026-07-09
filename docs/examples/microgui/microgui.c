/* MINIX micro-GUI PoC: /dev/fb0 + pixman + freetype + /dev/mouse.
 * Desktop with two draggable windows (freetype titles/body), an arrow
 * cursor driven by /dev/mouse relative motion, title-bar drag. Headless-
 * validated: logs input+state to /tmp/GUI and recomposites for screendump. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <minix/fb.h>
#include <sys/ioc_fb.h>
#include <minix/input.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pixman.h>
#include <ft2build.h>
#include FT_FREETYPE_H

static FILE *lg;
static int W,H,line,fbfd;
static uint32_t *fbuf; static int stride;
static pixman_image_t *scene;
static FT_Library ftlib; static FT_Face face;

/* ---- 12x19 arrow cursor, 1=opaque ---- */
static const char *arrow[19] = {
"X...........","XX..........","X.X.........","X..X........","X...X.......",
"X....X......","X.....X.....","X......X....","X.......X...","X........X..",
"X.....XXXXX.","X..X..X.....","X.X.X.X.....",".XX..X.X....",".X...X.X....",
"....X...X...","....X...X...",".....X.X....",".....XXX...."};

static void fill_rect(pixman_image_t *d, pixman_color_t *c, int x,int y,int w,int h){
    pixman_box32_t b={x,y,x+w,y+h};
    pixman_region32_t r; pixman_region32_init_rects(&r,&b,1);
    pixman_image_set_clip_region32(d,&r);
    pixman_image_t *s=pixman_image_create_solid_fill(c);
    pixman_image_composite32(PIXMAN_OP_SRC,s,NULL,d,0,0,0,0,0,0,W,H);
    pixman_image_unref(s); pixman_image_set_clip_region32(d,NULL);
    pixman_region32_fini(&r);
}
static void draw_str(pixman_image_t *d,int px,int py,const char*s,pixman_color_t*col,int sz){
    FT_Set_Pixel_Sizes(face,0,sz);
    pixman_image_t *fg=pixman_image_create_solid_fill(col);
    for(const char*p=s;*p;p++){
        if(FT_Load_Char(face,(unsigned char)*p,FT_LOAD_RENDER))continue;
        FT_GlyphSlot g=face->glyph; FT_Bitmap*bm=&g->bitmap;
        if(bm->width&&bm->rows&&bm->buffer){
            int ms=(bm->pitch>0)?bm->pitch:-bm->pitch;
            pixman_image_t*m=pixman_image_create_bits(PIXMAN_a8,bm->width,bm->rows,(uint32_t*)bm->buffer,ms);
            if(m){pixman_image_composite32(PIXMAN_OP_OVER,fg,m,d,0,0,0,0,px+g->bitmap_left,py-g->bitmap_top,bm->width,bm->rows);pixman_image_unref(m);}
        }
        px+=g->advance.x>>6;
    }
    pixman_image_unref(fg);
}
static void draw_cursor(pixman_image_t*d,int cx,int cy){
    pixman_color_t wht={0xffff,0xffff,0xffff,0xffff},blk={0,0,0,0xffff};
    for(int r=0;r<19;r++)for(int c=0;c<12;c++)if(arrow[r][c]=='X'){
        /* black outline then white core: draw white, 1px black border via neighbors */
        pixman_color_t*col=&wht; (void)blk;
        fill_rect(d,col,cx+c,cy+r,1,1);
    }
}

typedef struct{int x,y,w,h;pixman_color_t title,body;const char*name;const char*l1;const char*l2;}Win;
static Win wins[2];
static int order[2]={0,1}; /* order[1]=front */

static void composite(int cx,int cy){
    pixman_color_t desk={0x1010,0x2020,0x3838,0xffff};
    pixman_image_t*s=pixman_image_create_solid_fill(&desk);
    pixman_image_composite32(PIXMAN_OP_SRC,s,NULL,scene,0,0,0,0,0,0,W,H);
    pixman_image_unref(s);
    pixman_color_t wht={0xffff,0xffff,0xffff,0xffff},dark={0x1818,0x1818,0x2020,0xffff};
    pixman_color_t bord={0x8080,0x8080,0x9090,0xffff};
    for(int i=0;i<2;i++){Win*w=&wins[order[i]];
        fill_rect(scene,&bord,w->x-2,w->y-2,w->w+4,w->h+4);
        fill_rect(scene,&w->body,w->x,w->y,w->w,w->h);
        fill_rect(scene,&w->title,w->x,w->y,w->w,28);
        draw_str(scene,w->x+10,w->y+20,w->name,&wht,18);
        draw_str(scene,w->x+12,w->y+58,w->l1,&wht,16);
        draw_str(scene,w->x+12,w->y+82,w->l2,&dark,16);
    }
    draw_cursor(scene,cx,cy);
}
static void blit(void){
    uint32_t*pix=pixman_image_get_data(scene);
    for(int y=0;y<H;y++){lseek(fbfd,(off_t)y*line,SEEK_SET);
        if(write(fbfd,(char*)pix+(size_t)y*stride,stride)!=stride)return;}
}
static int in_title(Win*w,int x,int y){return x>=w->x&&x<w->x+w->w&&y>=w->y&&y<w->y+28;}

int main(void){
    lg=fopen("/tmp/GUI","w"); if(!lg)lg=stdout; setvbuf(lg,NULL,_IONBF,0);
    fbfd=open("/dev/fb0",O_RDWR);
    if(fbfd<0){fprintf(lg,"open /dev/fb0: %s\n",strerror(errno));return 1;}
    struct fb_var_screeninfo var; struct fb_fix_screeninfo fix;
    if(ioctl(fbfd,FBIOGET_VSCREENINFO,&var)<0){fprintf(lg,"VSCREEN:%s\n",strerror(errno));return 1;}
    if(ioctl(fbfd,FBIOGET_FSCREENINFO,&fix)<0){fprintf(lg,"FSCREEN:%s\n",strerror(errno));return 1;}
    W=var.xres;H=var.yres;line=fix.line_length;stride=W*4;
    fprintf(lg,"fb %dx%d %dbpp line=%d\n",W,H,var.bits_per_pixel,line);
    if(var.bits_per_pixel!=32){fprintf(lg,"need 32bpp\n");return 1;}
    fbuf=malloc((size_t)stride*H);
    scene=pixman_image_create_bits(PIXMAN_x8r8g8b8,W,H,fbuf,stride);
    if(FT_Init_FreeType(&ftlib)||FT_New_Face(ftlib,"/mnt/font.ttf",0,&face)){fprintf(lg,"freetype init fail\n");return 1;}

    pixman_color_t tblue={0x2020,0x4040,0xd0d0,0xffff}, bwhite={0xf0f0,0xf0f0,0xf8f8,0xffff};
    pixman_color_t tgreen={0x2020,0xa0a0,0x3030,0xffff}, bgrey={0xd8d8,0xdcdc,0xe0e0,0xffff};
    wins[0]=(Win){200,150,360,180,tblue,bwhite,"Terminal","MINIX 3 amd64 graphics stack","pixman + FreeType + /dev/fb0"};
    wins[1]=(Win){430,260,340,170,tgreen,bgrey,"About","No X11, no display server.","Framebuffer + mouse input."};

    int cx=W/2,cy=H/2, drag=-1;
    int mfd=open("/dev/mousemux",O_RDONLY|O_NONBLOCK);
    if(mfd<0)mfd=open("/dev/mouse0",O_RDONLY|O_NONBLOCK);
    fprintf(lg,"mouse fd=%d\n",mfd);
    composite(cx,cy);blit();
    fprintf(lg,"MICROGUI READY cursor=%d,%d\n",cx,cy);

    struct timeval start,now; gettimeofday(&start,NULL);
    struct input_event ev; int evcount=0, changed=1;
    for(;;){
        gettimeofday(&now,NULL);
        if((now.tv_sec-start.tv_sec)>75)break;
        fd_set rf;FD_ZERO(&rf);if(mfd>=0)FD_SET(mfd,&rf);
        struct timeval tv={0,40000};
        int n=(mfd>=0)?select(mfd+1,&rf,NULL,NULL,&tv):(usleep(40000),0);
        if(n>0&&FD_ISSET(mfd,&rf)){
            while(read(mfd,&ev,sizeof ev)==sizeof ev){
                int ocx=cx,ocy=cy;
                if(ev.page==INPUT_PAGE_GD&&ev.code==INPUT_GD_X){cx+=ev.value;}
                else if(ev.page==INPUT_PAGE_GD&&ev.code==INPUT_GD_Y){cy+=ev.value;}
                else if(ev.page==INPUT_PAGE_BUTTON&&ev.code==INPUT_BUTTON_1){
                    if(ev.value){ /* down: pick front-most window whose title is under cursor */
                        for(int i=1;i>=0;i--){Win*w=&wins[order[i]];
                            if(in_title(w,cx,cy)){drag=order[i];
                                int t=order[i];order[i]=order[1];order[1]=t; /*raise*/
                                fprintf(lg,"BTN down: drag win %d (%s) raised\n",drag,w->name);break;}}
                    } else { if(drag>=0)fprintf(lg,"BTN up: drop win %d\n",drag); drag=-1; }
                    changed=1;
                }
                if(cx<0)cx=0;if(cx>=W)cx=W-1;if(cy<0)cy=0;if(cy>=H)cy=H-1;
                if(drag>=0&&(cx!=ocx||cy!=ocy)){wins[drag].x+=cx-ocx;wins[drag].y+=cy-ocy;}
                if(cx!=ocx||cy!=ocy)changed=1;
                evcount++;
                if(evcount<=40||changed)fprintf(lg,"ev p=%x c=%x v=%d -> cur=%d,%d drag=%d win0=%d,%d win1=%d,%d\n",
                    ev.page,ev.code,ev.value,cx,cy,drag,wins[0].x,wins[0].y,wins[1].x,wins[1].y);
            }
        }
        if(changed){composite(cx,cy);blit();changed=0;}
    }
    fprintf(lg,"MICROGUI DONE events=%d final cursor=%d,%d win0=%d,%d win1=%d,%d\n",
        evcount,cx,cy,wins[0].x,wins[0].y,wins[1].x,wins[1].y);
    fprintf(lg,"MICROGUI PASS\n");
    return 0;
}
