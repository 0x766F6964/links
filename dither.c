/* dither.c
 * Dithering
 * (c) 2000-2002 Karel 'Clock' Kulhavy
 * This file is a part of the Links program, released under GPL.
 */

#include "cfg.h"

#ifdef G

#include "links.h"

#include <math.h>

/* The input of dithering function is 3 times 16-bit value. The value is
 * proportional to light that will go out of the monitor. Only in this space it
 * is possible to dither accurately because distributing the error means maintaining
 * the photon count (blurring caused by human eye from big distance preservers photon
 * count, just spreads the photons a little around)
 * The 8-bit dithering functions are to be used only for dithering text.
 */

/* This source does dithering and rounding of images (in photon space) into
 * struct bitmap. It also computes colors given r,g,b.
 */

/* No dither function destroys the passed bitmap */
/* All dither functions take format in booklike order without inter-line gaps.
 * red, green, blue order. Input bytes=3*y*x. Takes x and y from bitmap.
 */

/* The input of dithering function is 3 times 8-bit value. The value is
 * proportional to desired input into graphics driver (which is in fact
 * proportional to monitor's input voltage for graphic drivers that do not
 * pollute the picture with gamma correction)
 */

/* Dithering algorithm: Floyd-Steinberg error distribution. The used
 * coefficients are depicted in the following table. The empty box denotes the
 * originator pixel that generated the error.
 *
 *                    +----+----+
 *                    |    |7/16|
 *               +----+----+----+
 *               |3/16|5/16|1/16|
 *               +----+----+----+
 */

/* We assume here int holds at least 32 bits */
static int *red_table = DUMMY, *green_table = DUMMY, *blue_table = DUMMY;
static int table_16 = 1;

/* If we want to represent some 16-bit from-screen-light, it would require certain display input
 * value (0-255 red, 0-255 green, 0-255 blue), possibly not a whole number. [red|green|blue]_table
 * translares 16-bit light to the nearest index (that should be fed into the
 * display). Nearest is meant in realm of numbers that are proportional to
 * display input. The table also says what will be the real value this rounded
 * display input yields. index is in
 * bits 16-31, real light value is in bits 0-15. real light value is 0 (no
 * photons) to 65535 (maximum photon flux). This is subtracted from wanted
 * value and error remains which is the distributed into some neighboring
 * pixels.
 *
 * Index memory organization
 * -------------------------
 *	1 byte per pixel: obvious. The output byte is OR of all three LSB's from red_table,
 *		green_table, blue_table
 *	2 bytes per pixel: cast all three values to unsigned  short, OR them together
 *		and dump the short into the memory
 *	3 and 4 bytes per pixel: LSB's contain the red, green, and blue bytes.
 */

/* These tables allow the most precise dithering possible:
 * a) Rouding is performed always to perceptually nearest value, not to
 *    nearest light flux
 * b) error addition is performed in photon space to maintain fiedlity
 * c) photon space addition from b) is performed with 16 bits thus not
 *    degrading 24-bit images
 */

/* We assume here unsigned short holds at least 16 bits */
static unsigned short round_red_table[256];
static unsigned short round_green_table[256];
static unsigned short round_blue_table[256];
/* Transforms sRGB red, green, blue (0-255) to light of nearest voltage to
 * voltage appropriate to given sRGB coordinate.
 */

void (*round_fn)(unsigned short *my_restrict in, struct bitmap *out);
/* When you finish the stuff with dither_start, dither_restart, just do "if (dregs) mem_free(dregs);" */
static void (*dither_fn_internal)(unsigned short *my_restrict in, struct bitmap *out, int *dregs);


     /* prototypes */
static void dither_1byte(unsigned short *my_restrict, struct bitmap *, int *);   /* DITHER_TEMPLATE */
static void round_1byte(unsigned short *my_restrict, struct bitmap *);           /* ROUND_TEMPLATE */
static void dither_2byte(unsigned short *my_restrict, struct bitmap *, int *);   /* DITHER_TEMPLATE */
static void round_2byte(unsigned short *my_restrict, struct bitmap *);           /* ROUND_TEMPLATE */
static void dither_195(unsigned short *my_restrict, struct bitmap *, int *);   /* DITHER_TEMPLATE */
static void round_195(unsigned short *my_restrict, struct bitmap *);           /* ROUND_TEMPLATE */
static void dither_451(unsigned short *my_restrict, struct bitmap *, int *);   /* DITHER_TEMPLATE */
static void round_451(unsigned short *my_restrict, struct bitmap *);           /* ROUND_TEMPLATE */
static void dither_196(unsigned short *my_restrict, struct bitmap *, int *);   /* DITHER_TEMPLATE */
static void round_196(unsigned short *my_restrict, struct bitmap *);           /* ROUND_TEMPLATE */
static void dither_452(unsigned short *my_restrict, struct bitmap *, int *);   /* DITHER_TEMPLATE */
static void round_452(unsigned short *my_restrict, struct bitmap *);           /* ROUND_TEMPLATE */
static void dither_708(unsigned short *my_restrict, struct bitmap *, int *);   /* DITHER_TEMPLATE */
static void round_708(unsigned short *my_restrict, struct bitmap *);           /* ROUND_TEMPLATE */
static long color_332(int);
static long color_121(int);
static long color_pass_rgb(int);
static long color_888_bgr(int);
/*static void pass_bgr(unsigned short *, struct bitmap *);*/
static long color_8888_bgr0(int);
static long color_8888_0bgr(int);
static long color_8888_0rgb(int);
/*static void pass_0bgr(unsigned short *, struct bitmap *);*/
static long color_555be(int);
static long color_555(int);
static long color_565be(int);
static long color_565(int);
/*static void make_8_table(int *, double);*/
static void make_16_table(int *, int, int, float_double, int, int);
static void make_red_table(int, int, int, int);
static void make_green_table(int, int, int, int);
static void make_blue_table(int, int, int, int);
static void make_round_tables(void);

int slow_fpu = -1;

#define LIN \
	r+=(int)(in[0]);\
	g+=(int)(in[1]);\
	b+=(int)(in[2]);\
	in+=3;

/* EMPIRE IMAGINE FEAR */
#define LTABLES \
	{\
		int rc=r,gc=g,bc=b;\
		if ((unsigned)rc>65535) rc=rc<0?0:65535;\
		if ((unsigned)gc>65535) gc=gc<0?0:65535;\
		if ((unsigned)bc>65535) bc=bc<0?0:65535;\
		if (table_16) {\
			rt=red_table[rc];\
			gt=green_table[gc];\
			bt=blue_table[bc];\
		} else {\
			rt=red_table[rc >> 8];\
			gt=green_table[gc >> 8];\
			bt=blue_table[bc >> 8];\
		}\
	}\
	SAVE_CODE\
	rt=r-(rt&65535);\
	gt=g-(gt&65535);\
	bt=b-(bt&65535);\


#define BODY \
	LIN\
	LTABLES\
	r=bptr[3];\
	g=bptr[4];\
	b=bptr[5];\
	r+=rt;\
	g+=gt;\
	b+=bt;\
	rt+=8;\
	gt+=8;\
	bt+=8;\
	rt>>=4;\
	gt>>=4;\
	bt>>=4;\
	r-=9*rt;\
	g-=9*gt;\
	b-=9*bt;\
	bptr[3]=rt;\
	bptr[4]=gt;\
	bptr[5]=bt;

#define BODYR \
	LIN\
	LTABLES\
	rt+=8;\
	gt+=8;\
	bt+=8;\
	rt>>=4;\
	gt>>=4;\
	bt>>=4;\
	bptr[-3]+=3*rt;\
	bptr[-2]+=3*gt;\
	bptr[-1]+=3*bt;\
	*bptr+=5*rt;\
	bptr[1]+=5*gt;\
	bptr[2]+=5*bt;

#define BODYC \
	LIN\
	LTABLES\
	r=rt;\
	g=gt;\
	b=bt;

#define BODYL \
	bptr=dregs;\
	r=bptr[0];\
	g=bptr[1];\
	b=bptr[2];\
	BODY\
	bptr[0]=5*rt;\
	bptr[1]=5*gt;\
	bptr[2]=5*bt;\
	bptr+=3;

#define BODYI \
	BODY\
	bptr[0]+=5*rt;\
	bptr[1]+=5*gt;\
	bptr[2]+=5*bt;\
	bptr[-3]+=3*rt;\
	bptr[-2]+=3*gt;\
	bptr[-1]+=3*bt;\
	bptr+=3;

#define DITHER_TEMPLATE(template_name) \
	static void template_name(unsigned short *my_restrict in, struct bitmap *out, int *dregs)\
		{\
		int r,g,b,o,rt,gt,bt,y,x;\
		unsigned char *my_restrict outp=out->data;\
		int *my_restrict bptr;\
		int skip=out->skip-SKIP_CODE;\
\
		o=0;o=o; /*warning go away */\
		switch(out->x){\
\
			case 0:\
			return;\
\
			case 1:\
			r=g=b=0;\
			for (y=out->y;y;y--){\
				BODYC\
				outp+=skip;\
			}\
			break;\
\
			default:\
			for (y=out->y;y;y--){\
				BODYL\
				for (x=out->x-2;x;x--){\
					BODYI\
				}\
				BODYR\
				outp+=skip;\
			}\
			break;\
		}\
	}

#define ROUND_TEMPLATE(template_name)\
	static void template_name(unsigned short *my_restrict in, struct bitmap *out)\
	{\
		int rt,gt,bt,o,x,y;\
		unsigned char *my_restrict outp=out->data;\
		int skip=out->skip-SKIP_CODE;\
	\
		o=0;o=o; /*warning go away */\
		if (table_16) {\
			for (y=out->y;y;y--){\
				for (x=out->x;x;x--){\
					rt=red_table[limit_16(in[0])];\
					gt=green_table[limit_16(in[1])];\
					bt=blue_table[limit_16(in[2])];\
					in+=3;\
					SAVE_CODE\
				}\
				outp+=skip;\
			}\
		} else {\
			for (y=out->y;y;y--){\
				for (x=out->x;x;x--){\
					rt=red_table[limit_16(in[0]) >> 8];\
					gt=green_table[limit_16(in[1]) >> 8];\
					bt=blue_table[limit_16(in[2]) >> 8];\
					in+=3;\
					SAVE_CODE\
				}\
				outp+=skip;\
			}\
		}\
	}

/* Expression determining line length in bytes */
#define SKIP_CODE out->x

/* Code with input in rt, gt, bt (values from red_table, green_table, blue_table)
 * that saves appropriate code on *outp (unsigned char *outp). We can use int o;
 * as a scratchpad.
 */
#define SAVE_CODE \
	o=rt|gt|bt;\
	*outp++=(o>>16);

DITHER_TEMPLATE(dither_1byte)
ROUND_TEMPLATE(round_1byte)

#undef SKIP_CODE
#undef SAVE_CODE

#define SKIP_CODE out->x*2
#define SAVE_CODE \
	o=rt|gt|bt;\
	*(unsigned short *)outp=(o>>16);\
	outp+=2;

DITHER_TEMPLATE(dither_2byte)
ROUND_TEMPLATE(round_2byte)
#undef SAVE_CODE
#undef SKIP_CODE

/* B G R */
#define SKIP_CODE out->x*3;
#define SAVE_CODE outp[0]=bt>>16;\
	outp[1]=gt>>16;\
	outp[2]=rt>>16;\
	outp+=3;
DITHER_TEMPLATE(dither_195)
ROUND_TEMPLATE(round_195)
#undef SAVE_CODE
#undef SKIP_CODE

/* R G B */
#define SKIP_CODE out->x*3;
#define SAVE_CODE *outp=rt>>16;\
	outp[1]=gt>>16;\
	outp[2]=bt>>16;\
	outp+=3;
DITHER_TEMPLATE(dither_451)
ROUND_TEMPLATE(round_451)
#undef SAVE_CODE
#undef SKIP_CODE

/* B G R 0 */
#define SKIP_CODE out->x*4;
#define SAVE_CODE *outp=bt>>16;\
	outp[1]=gt>>16;\
	outp[2]=rt>>16;\
	outp[3]=0;\
	outp+=4;
DITHER_TEMPLATE(dither_196)
ROUND_TEMPLATE(round_196)
#undef SAVE_CODE
#undef SKIP_CODE

/* 0 B G R */
#define SKIP_CODE out->x*4;
#define SAVE_CODE *outp=0;\
	outp[1]=bt>>16;\
	outp[2]=gt>>16;\
	outp[3]=rt>>16;\
	outp+=4;
DITHER_TEMPLATE(dither_452)
ROUND_TEMPLATE(round_452)
#undef SAVE_CODE
#undef SKIP_CODE

/* 0 R G B */
#define SKIP_CODE out->x*4;
#define SAVE_CODE *outp=0;\
	outp[1]=rt>>16;\
	outp[2]=gt>>16;\
	outp[3]=bt>>16;\
	outp+=4;
DITHER_TEMPLATE(dither_708)
ROUND_TEMPLATE(round_708)
#undef SAVE_CODE
#undef SKIP_CODE



/* For 256-color cube */
static long color_332(int rgb)
{
	int r,g,b;
	long ret = 0;

	r=(rgb>>16)&255;
	g=(rgb>>8)&255;
	b=rgb&255;
	r=(r*7+127)/255;
	g=(g*7+127)/255;
	b=(b*3+127)/255;

	*(unsigned char *)&ret=(r<<5)|(g<<2)|b;
	return ret;

}

static long color_121(int rgb)
{
	int r,g,b;
	long ret = 0;

	r=(rgb>>16)&255;
	g=(rgb>>8)&255;
	b=rgb&255;
	r=(r+127)/255;
	g=(3*g+127)/255;
	b=(b+127)/255;
	*(unsigned char *)&ret=(r<<3)|(g<<1)|b;
	return ret;

}

static long color_pass_rgb(int rgb)
{
	long ret = 0;

	((unsigned char *)&ret)[0]=rgb>>16;
	((unsigned char *)&ret)[1]=rgb>>8;
	((unsigned char *)&ret)[2]=(unsigned char)rgb;

	return ret;

}

static long color_888_bgr(int rgb)
{
	long ret = 0;

	((unsigned char *)&ret)[0]=(unsigned char)rgb;
	((unsigned char *)&ret)[1]=rgb>>8;
	((unsigned char *)&ret)[2]=rgb>>16;

	return ret;
}

#if 0
/* Long live the Manchester Modulation! */
static void pass_bgr(unsigned short *in, struct bitmap *out)
{
	int skip=out->skip-3*out->x,y,x;
	unsigned char *outp=out->data;

	for (y=out->y;y;y--){
		for (x=out->x;x;x--){
			outp[0]=in[2];
			outp[1]=in[1];
			outp[2]=in[0];
			outp+=3;
			in+=3;
		}
		outp+=skip;
	}
}
#endif

static long color_8888_bgr0(int rgb)
{
	long ret = 0;

	((unsigned char *)&ret)[0]=(unsigned char)rgb;
	((unsigned char *)&ret)[1]=rgb>>8;
	((unsigned char *)&ret)[2]=rgb>>16;
	((unsigned char *)&ret)[3]=0;

	return ret;
}

/* Long live the sigma-delta modulator! */
static long color_8888_0bgr(int rgb)
{
	long ret = 0;

	/* Atmospheric lightwave communication rulez */
	((unsigned char *)&ret)[0]=0;
	((unsigned char *)&ret)[1]=(unsigned char)rgb;
	((unsigned char *)&ret)[2]=rgb>>8;
	((unsigned char *)&ret)[3]=rgb>>16;

	return ret;
}

/* Long live His Holiness The 14. Dalai Lama Taendzin Gjamccho! */
/* The above line will probably cause a ban of this browser in China under
 * the capital punishment ;-) */
static long color_8888_0rgb(int rgb)
{
	long ret = 0;

	/* Chokpori Dharamsala Lhasa Laddakh */
	((unsigned char *)&ret)[0]=0;
	((unsigned char *)&ret)[1]=rgb>>16;
	((unsigned char *)&ret)[2]=rgb>>8;
	((unsigned char *)&ret)[3]=(unsigned char)rgb;

	return ret;
}

#if 0
/* We assume unsgned short holds at least 16 bits. */
static void pass_0bgr(unsigned short *in, struct bitmap *out)
{
	int skip=out->skip-4*out->x,y,x;
	unsigned char *outp=out->data;

	for (y=out->y;y;y--){
		for (x=out->x;x;x--){
			outp[0]=0;
			outp[1]=in[2]>>8;
			outp[2]=in[1]>>8;
			outp[3]=in[0]>>8;
			outp+=4;
			in+=3;
		}
		outp+=skip;
	}
}
#endif

/* We assume long holds at least 32 bits */
static long color_555be(int rgb)
{
	int r=(rgb>>16)&255;
	int g=(rgb>>8)&255;
	int b=(rgb)&255;
	int i;
	long ret = 0;

	r=(r*31+127)/255;
	g=(g*31+127)/255;
	b=(b*31+127)/255;
	i=(r<<10)|(g<<5)|b;
	((unsigned char *)&ret)[0]=i>>8;
	((unsigned char *)&ret)[1]=(unsigned char)i;
	return ret;
}

/* We assume long holds at least 32 bits */
static long color_555(int rgb)
{
	int r=(rgb>>16)&255;
	int g=(rgb>>8)&255;
	int b=(rgb)&255;
	int i;
	long ret = 0;

	r=(r*31+127)/255;
	g=(g*31+127)/255;
	b=(b*31+127)/255;
	i=(r<<10)|(g<<5)|b;
	((unsigned char *)&ret)[0]=(unsigned char)i;
	((unsigned char *)&ret)[1]=i>>8;
	return ret;
}

static long color_565be(int rgb)
{
	int r,g,b;
	long ret = 0;
	int i;

	r=(rgb>>16)&255;
	g=(rgb>>8)&255;
	/* Long live the PIN photodiode */
	b=rgb&255;

	r=(r*31+127)/255;
	g=(g*63+127)/255;
	b=(b*31+127)/255;
	i = (r<<11)|(g<<5)|b;
	((unsigned char *)&ret)[0]=i>>8;
	((unsigned char *)&ret)[1]=(unsigned char)i;
	return ret;
}

static long color_565(int rgb)
{
	int r,g,b;
	long ret = 0;
	int i;

	r=(rgb>>16)&255;
	g=(rgb>>8)&255;
	/* Long live the PIN photodiode */
	b=rgb&255;

	r=(r*31+127)/255;
	g=(g*63+127)/255;
	b=(b*31+127)/255;
	i=(r<<11)|(g<<5)|b;
	((unsigned char *)&ret)[0]=(unsigned char)i;
	((unsigned char *)&ret)[1]=i>>8;
	return ret;
}

/* rgb = r*65536+g*256+b */
/* The selected color_fn returns a long.
 * When we have for example 2 bytes per pixel, we make them in the memory,
 * then copy them to the beginning of the memory occupied by the long
 * variable, and return that long variable.
 */
long (*get_color_fn(int depth))(int rgb)
{
	switch(depth)
	{
		case 33:
			return color_121;
			break;

		case 65:
			return color_332;
			break;

		case 122:
			return color_555;
			break;

		case 378:
			return color_555be;
			break;

		case 130:
			return color_565;
			break;

		case 386:
			return color_565be;
			break;

		case 451:
			return color_pass_rgb;
			break;

		case 195:
			return color_888_bgr;
			break;

		case 452:
			return color_8888_0bgr;
			break;

		case 196:
			return color_8888_bgr0;
			break;

		case 708:
			return color_8888_0rgb;
			break;

		default:
			return NULL;
			break;

	}
}

#if 0
static void make_8_table(int *table, double gamma)
{
	int i,light0;
	float_double light;
	const float_double inv_255=1/255.;

	for (i=0;i<256;i++){
		light=fd_pow((float_double)i*inv_255,gamma);
		/* Long live the Nipkow Disk */
		light0=65535*light;
		if (light0<0) light0=0;
		if (light0>65535) light0=65535;
		table[i]=light0;
	}
}
#endif

/* Gamma says that light=electricity raised to gamma */
/* dump_t2c means memory organization defined in comment for
 * red_table on the top of dither.c */
/* dump_t2c is taken into account only if t2c is defined. */
static void make_16_table(int *table, int bits, int pos, float_double gamma, int dump_t2c,
	int bigendian)
{
	int j,light_val,grades=(1<<bits)-1,grade;
	float_double voltage;
	float_double rev_gamma=1/gamma;
	const float_double inv_65535=(float_double)(1/65535.);
	int last_grade, last_content;
	uttime start_time = get_time();
	int sample_state = 0;
	int x_slow_fpu = slow_fpu;
	if (gamma_bits != 2) x_slow_fpu = !gamma_bits;

	repeat_loop:
	last_grade=-1;
	last_content=0;

	for (j=0;j<65536;j++){
		if (x_slow_fpu) {
			if (x_slow_fpu == 1) {
				if (j & 255) {
					table[j] = last_content;
					continue;
				}
			} else {
				if (!(j & (j - 1))) {
					uttime now = get_time();
					if (!sample_state) {
						if (now != start_time) start_time = now, sample_state = 1;
					} else {
						if (now - start_time > SLOW_FPU_DETECT_THRESHOLD && (now - start_time) * 65536 / j > SLOW_FPU_MAX_STARTUP / 3) {
							x_slow_fpu = 1;
							goto repeat_loop;
						}
					}
				}
			}
		}
		voltage=fd_pow(j*inv_65535,rev_gamma);
		/* Determine which monitor input voltage is equivalent
		 * to said photon flux level
		 */

		grade=(int)(voltage*grades+(float_double)0.5);
		if (grade==last_grade){
			table[j]=last_content;
			continue;
		}
		last_grade=grade;
		voltage=(float_double)grade/grades;
		/* Find nearest voltage to this voltage. Finding nearest voltage, not
		 * nearest photon flux ensures the dithered pixels will be perceived to be
		 * near. The voltage input into the monitor was intentionally chosen by
		 * generations of television engineers to roughly comply with eye's
		 * response, thus minimizing and unifying noise impact on transmitted
		 * signal. This is only marginal enhancement however it sounds
		 * kool ;-) (and is kool)
		 */

		light_val=(int)(fd_pow(voltage,gamma)*65535+(float_double)0.5);
		/* Find out what photon flux this index represents */

		if (light_val<0) light_val=0;
		if (light_val>65535) light_val=65535;
		/* Clip photon flux for safety */

		if (bigendian) {
			unsigned val, val2;
			val = grade<<pos;
			val2 = (val>>8) | ((val&0xff)<<8);
			last_content=light_val|((unsigned)val2<<16U);
		}else{
			last_content=light_val|((unsigned)grade<<(pos+16U));
		}

		table[j]=last_content;
		/* Save index and photon flux. */
	}
	if (x_slow_fpu == -1) slow_fpu = 0;	/* if loop passed once without
		detecting slow fpu, always assume fast FPU */
	if (gamma_bits == 2 && x_slow_fpu == 1) slow_fpu = 1;
}

static void make_red_table(int bits, int pos, int dump_t2c, int be)
{
	red_table = mem_realloc(red_table, 65536 * sizeof(*red_table));
	make_16_table(red_table,bits,pos,(float_double)display_red_gamma,dump_t2c, be);
}

static void make_green_table(int bits, int pos, int dump_t2c, int be)
{
	green_table = mem_realloc(green_table, 65536 * sizeof(*green_table));
	make_16_table(green_table,bits,pos,(float_double)display_green_gamma,dump_t2c, be);
}

static void make_blue_table(int bits, int pos,int dump_t2c, int be)
{
	blue_table = mem_realloc(blue_table, 65536 * sizeof(*blue_table));
	make_16_table(blue_table,bits,pos,(float_double)display_blue_gamma, dump_t2c, be);
}

void dither(unsigned short *in, struct bitmap *out)
{
	int *dregs;

	if ((unsigned)out->x > MAXINT / 3 / sizeof(*dregs)) overalloc();
	dregs=mem_calloc(out->x*3*sizeof(*dregs));
	(*dither_fn_internal)(in, out, dregs);
	mem_free(dregs);
}

/* For functions that do dithering.
 * Returns allocated dregs. */
int *dither_start(unsigned short *in, struct bitmap *out)
{
	int *dregs;

	if ((unsigned)out->x > MAXINT / 3 / sizeof(*dregs)) overalloc();
	dregs=mem_calloc(out->x*3*sizeof(*dregs));
	(*dither_fn_internal)(in, out, dregs);
	return dregs;
}

void dither_restart(unsigned short *in, struct bitmap *out, int *dregs)
{
	(*dither_fn_internal)(in, out, dregs);
}

static void make_round_tables(void)
{
	int a;
	/* ICC bug */
	icc_volatile unsigned short v;

	for (a=0;a<256;a++){
		/* a is sRGB coordinate */
		v=ags_8_to_16((unsigned char)a,(float)((float)user_gamma/(float)sRGB_gamma));
		round_red_table[a]=red_table[v >> (8 - 8 * table_16)] & 0xffff;
		round_green_table[a]=green_table[v >> (8 - 8 * table_16)] & 0xffff;
		round_blue_table[a]=blue_table[v >> (8 - 8 * table_16)] & 0xffff;
	}
}

static void compress_tables(void)
{
	int i;
	int *rt, *gt, *bt;
	/*for (i = 0; i < 65536; i++) {
		fprintf(stderr, "16: %03d: %08x %08x %08x\n", i, red_table[i], green_table[i], blue_table[i]);
	}*/
	for (i = 0; i < 65536; i++) {
		if (red_table[i] != red_table[i & 0xff00] ||
		    green_table[i] != green_table[i & 0xff00] ||
		    blue_table[i] != blue_table[i & 0xff00])
			return;
	}
	table_16 = 0;
	rt = mem_alloc(256 * sizeof(*rt));
	gt = mem_alloc(256 * sizeof(*gt));
	bt = mem_alloc(256 * sizeof(*bt));
	for (i = 0; i < 256; i++) {
		rt[i] = red_table[i << 8];
		gt[i] = green_table[i << 8];
		bt[i] = blue_table[i << 8];
		/*fprintf(stderr, "8: %03d: %08x %08x %08x\n", i, rt[i], gt[i], bt[i]);*/
	}
	mem_free(red_table);
	mem_free(green_table);
	mem_free(blue_table);
	red_table = rt;
	green_table = gt;
	blue_table = bt;
}

/* Also makes up the dithering tables.
 * You may call it twice - it doesn't leak any memory.
 */
void init_dither(int depth)
{
	table_16 = 1;
	switch(depth){
		case 33:
		/* 4bpp, 1Bpp */
		make_red_table(1,3,0,0);
		make_green_table(2,1,0,0);
		make_blue_table(1,0,0,0);
		dither_fn_internal=dither_1byte;
		round_fn=round_1byte;
		break;

		case 65:
		/* 8 bpp, 1 Bpp */
		make_red_table(3,5,0,0);
		make_green_table(3,2,0,0);
		make_blue_table(2,0,0,0);
		dither_fn_internal=dither_1byte;
		round_fn=round_1byte;
		break;

		case 122:
		/* 15bpp, 2Bpp */
		make_red_table(5,10,1,0);
		make_green_table(5,5,1,0);
		make_blue_table(5,0,1,0);
		dither_fn_internal=dither_2byte;
		round_fn=round_2byte;
		break;

		case 378:
		/* 15bpp, 2Bpp, disordered (I have a mental disorder) */
		make_red_table(5,10,1,1);
		make_green_table(5,5,1,1);
		make_blue_table(5,0,1,1);
		dither_fn_internal=dither_2byte;
		round_fn=round_2byte;
		break;

		case 130:
		/* 16bpp, 2Bpp */
		make_red_table(5,11,1,0);
		make_green_table(6,5,1,0);
		make_blue_table(5,0,1,0);
		dither_fn_internal=dither_2byte;
		round_fn=round_2byte;
		break;

		case 386:
		/* 16bpp, 2Bpp, disordered */
		make_red_table(5,11,1,1);
		make_green_table(6,5,1,1);
		make_blue_table(5,0,1,1);
		dither_fn_internal=dither_2byte;
		round_fn=round_2byte;
		break;

		case 451:
		/* 24bpp, 3Bpp, misordered
		 * Even this is dithered!
		 * R G B
		 */
		make_red_table(8,0,0,0);
		make_green_table(8,0,0,0);
		make_blue_table(8,0,0,0);
		dither_fn_internal=dither_451;
		round_fn=round_451;
		break;

		case 195:
		/* 24bpp, 3Bpp
		 * Even this is dithered!
		 * B G R
		 */
		make_red_table(8,0,0,0);
		make_green_table(8,0,0,0);
		make_blue_table(8,0,0,0);
		dither_fn_internal=dither_195;
		round_fn=round_195;
		break;

		case 452:
		/* 24bpp, 4Bpp, misordered
		 * Even this is dithered!
		 * 0 B G R
		 */
		make_red_table(8,0,0,0);
		make_green_table(8,0,0,0);
		make_blue_table(8,0,0,0);
		dither_fn_internal=dither_452;
		round_fn=round_452;
		break;

		case 196:
		/* 24bpp, 4Bpp
		 * Even this is dithered!
		 * B G R 0
		 */
		make_red_table(8,0,0,0);
		make_green_table(8,0,0,0);
		make_blue_table(8,0,0,0);
		dither_fn_internal=dither_196;
		round_fn=round_196;
		break;

		case 708:
		/* 24bpp, 4Bpp
		 * Even this is dithered!
		 * 0 R G B
		 */
		make_red_table(8,0,0,0);
		make_green_table(8,0,0,0);
		make_blue_table(8,0,0,0);
		dither_fn_internal=dither_708;
		round_fn=round_708;
		break;

		default:
		internal("Graphics driver returned unsupported \
pixel memory organisation %d",depth);
	}
	compress_tables();
	make_round_tables();
}

/* Input is in sRGB space (unrounded, i. e. directly from HTML)
 * Output is linear 48-bit value (in photons) that has corresponding
 * voltage nearest to the voltage that would be procduced ideally
 * by the input value. */
void round_color_sRGB_to_48(unsigned short *my_restrict red, unsigned short *my_restrict green,
		unsigned short *my_restrict blue, int rgb)
{
	*red=round_red_table[(rgb>>16)&255];
	*green=round_green_table[(rgb>>8)&255];
	*blue=round_blue_table[rgb&255];
}

void free_dither(void)
{
	if (red_table) mem_free(red_table), red_table = DUMMY;
	if (green_table) mem_free(green_table), green_table = DUMMY;
	if (blue_table) mem_free(blue_table), blue_table = DUMMY;
}

#endif
