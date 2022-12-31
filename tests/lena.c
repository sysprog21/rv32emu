/*
 * This tiny C program outputs a 128x128 RGB image file to the standard output
 * using the portable pixmap file format (PPM).
 *
 * Source: https://bellard.org/ioccc_lena/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ACTX_SIGN 3
#define ACTX_VDATA 4
#define ACTX_LEN 5
#define ACTX_LEVEL 25
#define ACTX_IPRED 73
#define ACTX_UE_LEN 10
#define ACTX_COUNT2 166
#define ACTX_EOB2 61

static char *inp =
    "	{k/;	y{ q ; }	c {	@;	={ 	S}	c}	W;;	{4}	k "
    "|; w{	+9;{;	8; 9{	S;	/}	y{ K}	{;}	l{	{ ~{ ;	V}"
    "k}g< t{	E	v;M{ B}y}	<{7;/;	Y} t}kp; Y} $Ha{e} "
    "w};} R} /{>}a	;} ;	`	$W-}	D}B; e;f;*;	~;A;s "
    "O{	o;>{1; m{ `} R}]{ T} v}={ I} ; }a?&; A}$;W;R{u} `; j}W;"
    "s{e}	A;[	R;	X  P; 4 ,F;({<8{#;%}@J{)}	}o^*{u/{"
    "'}]{	*}	}	;{ r}	f	/;}e} }w{ ${{;,; @ d	$}];"
    ">(}	I{ d}	&;	U}	{	y;Y}	{ P{	R} T}_{ }R } l	{ T}"
    "';	|; ${=}	H} (}}8{cp{ s} #}+}	3}kF}<H	 .{ }G}"
    "x;	r	D c{; W; {	b;6; k{}B;*};	]} ~	{ ;;} !}}	x}"
    "v}n;^;	6V}Y{ h; ~	%*}! H; G{ r{ f;Y{ i}z} N  %}.{;	( "
    "	v} _}	h; 7;<}	^;Z;0; ;	<;<; M; N{	}	_{O} !{f{]{"
    "M{;A{}	0;S}${	@;x}y}@	L;1	t{ 3{c{s{_{	`{	D{ ]}"
    "!;	${	_J;v+ }	3{B; ]{	}	E6	.x{?+; {x; }v{$};6}T; "
    "O; ; (}X7}	j; @} :}#	c{ !{ }x	KXt} >; ?{ c; ;	W;	; l;} "
    "h}p}	i{ %	}P}	/{	*}	%L; ;	!{ S{ n} "
    "x;  { 1	J;v{	U}({	@ X{ k} H;4;e J	6;;v; G{{]	&{"
    "A d{ lM{;K;;	4-{}} p h{;	{	rW;	v{;	f}	}1{^&{9{"
    "{ ;~;n;q{	9 R	6{	{ u;a;	;	U;	;Y}	+}}2sk; 8	{	J"
    "K;'i;	;$;	W{	P!{{{P	} [;	(;Q; Un;+}g{C;{"
    "{	; <{	vS} b;6`} ?{+	%;	}n;q{ r}k; ;{c{ S} 2}"
    "~{	4;RW v} R;	kI}|; d; [ O}5; ;;}Z d	{ {&;h	o{ "
    "V	v ;	_{{/}  F{f{r{4{{?{ 4;S}	:;];E}	;	&} #e !{"
    ">{H; {O{ 0;} H;	p; w}>{1}{	-} 4;"
    "S}}	u L{ y} %;2  |{(}	/;,{ )}Y;g}	G}v;T}	};}i {{"
    "};[{ E{q} g;T{ ={}R;	k{ j;_;h}gPc;({	F;6}	}} 3	,}<; "
    "0	 P;{'t}u};		}U}s{8{ E} >{}E	{G{H :{  Yo"
    "g}	}F  D{ R{	 -;M?;= q}_ U	{ ;	 I	{ |{{}	 	1{"
    ",}{ x{{ U{ s;J}}	6{>7;,{ D{	{{ ;]}	;M; &}{ V}	"
    "n{&	T~;({	}[;	r{#	u{X 9;L; Uf})}   {T}		p{	N;	"
    ">{	>	}}D} m{1{	{}X; o}	w}$}	^v} K  f	,}	^3; "
    "{ @{_} _{	o;	4}	h}H;#.{	{}	;	<{ {G{ $;{ "
    "z {a{{D;	?|}{{ ;	`} }	Q}j;4} 	3{Q}	{	* ;}r{"
    "a}	} R{p @;  N{ {f; A;8}L	$}{ }}J{ }	k{r} { [; "
    "-;p{	I{ {	&}J;	T}	?{Z{>;	5>; ];  wz ^}	u;);	H}	; "
    "L	&;	V	E{1{g;C} V} ~;U; ^{	J; { /}	{;(}y} aK /}	.}"
    ";K;N{w{ `{	}T{l`; #;N{lX;	?; +}{ 	w{	;	q;	z;_;"
    "y} 8} 	&{X}	V{ WG}	,; [}U{	v{	Q;	w{	[	Y}N	Yu i{ "
    "{!A{}{ b0;	X~} ;-; 8{	E }	;F{	y{}{	";

#define IMG_SIZE_MAX_LOG2 20

#define DCT_BITS 10
#define DCT_SIZE_LOG2_MAX 5
#define DCT_SIZE_MAX 32       /* (1 << DCT_SIZE_LOG2_MAX) */
#define DCT_SIZE_MAX4 128     /* 4 * DCT_SIZE_MAX */
#define DCT_SIZE_MAX_SQ2 2048 /* 2 * DCT_SIZE_MAX^2 */

#define FREQ_MAX 63
#define SYM_COUNT 1968

static int img_data[3][1 << IMG_SIZE_MAX_LOG2];
static int a_ctx[ACTX_COUNT2];
static int a_low, a_range = 1, stride, y_scale, c_scale;
static int dct_coef[DCT_SIZE_MAX4];

int get_bit(int c)
{
    int v, *p = a_ctx + c * 2, b = *p + 1, s = b + p[1] + 1;
    if (a_range < SYM_COUNT) {
        a_range *= SYM_COUNT;
        a_low *= SYM_COUNT;
        if ((v = *inp)) {
            /* char conversion */
            a_low += (v - 1 - (v > 10) - (v > 13) - (v > 34) - (v > 92)) << 4;
            /* space conversion */
            v = *++inp;
            inp++;
            a_low += v < 33 ? (v ^ 8) * 2 % 5
                            : (v ^ 6) % 3 * 4 + (*inp++ ^ 8) * 2 % 5 + 4;
        }
    }
    /* 0 < range0 < a_range */
    v = a_range * b / s;
    if ((b = (a_low >= v))) {
        a_low -= v;
        a_range -= v;
    } else
        a_range = v;
    p[b]++;
    if (s > FREQ_MAX) {
        *p /= 2;
        p[1] /= 2;
    }
    return b;
}

/* positive number, Golomb encoding */
int get_ue(int c)
{
    int i = 0, v = 1;
    while (!get_bit(c + i))
        i++;
    while (i--)
        v += v + get_bit(ACTX_VDATA);
    return v - 1;
}

void idct(int *dst,
          int dst_stride,
          int *src,
          int src_stride,
          int stride2,
          int n,
          int rshift)
{
    for (int l = 0; l < n; l++)
        for (int i = 0; i < n; i++) {
            int sum = 1 << (rshift - 1);
            for (int j = 0; j < n; j++)
                sum += src[j * src_stride + l * stride2] *
                       dct_coef[(2 * i + 1) * j * DCT_SIZE_MAX / n %
                                DCT_SIZE_MAX4];
            dst[i * dst_stride + l * stride2] = sum >> rshift;
        }
}

static int buf1[DCT_SIZE_MAX_SQ2];
void decode_rec(int x, int y, int w_log2)
{
    int b;
    int w = 1 << w_log2, n = w * w;

    if ((w_log2 > DCT_SIZE_LOG2_MAX) || (w_log2 > 2 && get_bit(w_log2 - 3))) {
        w /= 2;
        for (int i = 0; i < 4; i++)
            decode_rec(x + i % 2 * w, y + i / 2 * w, w_log2 - 1);
        return;
    }

    int pred_idx = get_ue(ACTX_IPRED);
    for (int c_idx = 0; c_idx < 3; c_idx++) {
        int *out = img_data[c_idx] + y * stride + x;
        int c_idx1 = c_idx > 0;

        /* decode coefs */
        memset(buf1, 0, n * sizeof(int));
        for (int i = 0; i < n; i++) {
            if (get_bit(ACTX_EOB2 + w_log2 * 2 + c_idx1))
                break;
            i += get_ue(ACTX_LEN + c_idx1 * ACTX_UE_LEN);
            b = 1 - 2 * get_bit(ACTX_SIGN);
            buf1[i] =
                b *
                (get_ue(ACTX_LEVEL + (c_idx1 + (i < n / 8) * 2) * ACTX_UE_LEN) +
                 1) *
                (c_idx ? c_scale : y_scale);
        }

        /* DC prediction */
        if (!pred_idx) {
            int dc = 0;
            for (int i = 0; i < w; i++) {
                dc += y ? out[-stride + i] : 0;
                dc += x ? out[i * stride - 1] : 0;
            }
            *buf1 += x && y ? dc / 2 : dc;
        }

        /* horizontal */
        idct(buf1 + n, 1, buf1, 1, w, w, DCT_BITS);
        /* vertical */
        idct(out, stride, buf1 + n, w, 1, w, DCT_BITS + w_log2);

        if (!pred_idx)
            continue;

        /* directional prediction */
        int swap = pred_idx < 17, frac;
        int delta = swap ? 9 - pred_idx : pred_idx - 25;
        for (int i = 0; i < w; i++)
            for (int j = 0; j < w; j++) {
                for (int k = 0; k < 2; k++) {
                    int x1 = i * delta + delta;
                    frac = x1 & 7;
                    x1 = (x1 >> 3) + j + k;
                    if ((b = (x1 < 0)))
                        x1 = (x1 * 8 + delta / 2) / delta - 2;
                    x1 = x1 < w ? x1 : w - 1;
                    buf1[k] =
                        b ^ swap ? out[x1 * stride - 1] : out[-stride + x1];
                }
                out[swap ? j * stride + i : i * stride + j] +=
                    (*buf1 * (8 - frac) + buf1[1] * frac + 4) >> 3;
            }
    }
}

int main()
{
    int a = 0;
    int b = 74509276;
    for (int i = 0; i < 128; i++) {
        dct_coef[i + 96 & 127] = ((a >> 19) + 1) >> 1;
        int c = b;
        b = (2144896910LL * b >> 30) - a;
        a = c;
    }

    *dct_coef = 1024;
    int w_log2 = get_ue(ACTX_LEN);
    stride = 1 << w_log2;
    int h = stride - get_ue(ACTX_LEN);
    y_scale = get_ue(ACTX_LEN);
    c_scale = get_ue(ACTX_LEN);

    decode_rec(0, 0, w_log2);

    /* output */
    FILE *fp = fopen("lena.ppm", "wb");
    fprintf(fp, "P6 %d %d 255 ", stride, h);
    for (int i = 0; i < h * stride; i++) {
        int y = img_data[0][i], cg = img_data[1][i], co = img_data[2][i];
        int t = y - cg;
#define PUT(v) fprintf(fp, "%c", (v < 0) ? 0 : (v > 255) ? 255 : v)
        PUT(t + co);
        PUT(y + cg);
        PUT(t - co);
#undef PUT
    }
    fclose(fp);

    return 0;
}
