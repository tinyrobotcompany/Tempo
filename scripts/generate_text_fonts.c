/* Build with FreeType; input: Montserrat-Medium.ttf; output: LVGL Latin/Greek/Cyrillic fallbacks. */
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
int main(int argc,char **argv){
 if(argc!=2)return 1;
 FT_Library lib;FT_Face face;
 if(FT_Init_FreeType(&lib)||FT_New_Face(lib,argv[1],0,&face))return 2;
 puts("/* Generated Montserrat Medium fallback glyphs. SIL OFL: OFL.txt */\n#include \"text_fonts.h\"");
 const unsigned sizes[]={18,24,32,48};
 for(unsigned size_index=0;size_index<4;size_index++){
 unsigned size=sizes[size_index],count=0,offset=0;int desc[1400][6];unsigned chars[1400];
 if(FT_Set_Pixel_Sizes(face,0,size))return 3;
 printf("static const uint8_t bitmap_%u[]={\n",size);
 for(unsigned cp=160;cp<0x2070;cp++){
 if(cp>=0x530&&cp<0x2000)continue;
 if(!FT_Get_Char_Index(face,cp))continue;
 if(count>=1400||FT_Load_Char(face,cp,FT_LOAD_RENDER))return 4;
 FT_GlyphSlot g=face->glyph;chars[count]=cp-160;
 int *d=desc[count++];d[0]=offset;d[1]=g->advance.x/4;d[2]=g->bitmap.width;d[3]=g->bitmap.rows;d[4]=g->bitmap_left;d[5]=g->bitmap_top-g->bitmap.rows;
 unsigned half=0,byte=0;
 for(unsigned y=0;y<g->bitmap.rows;y++)for(unsigned x=0;x<g->bitmap.width;x++){
 unsigned pixel=g->bitmap.buffer[y*g->bitmap.pitch+x]>>4;
 if(!half){byte=pixel<<4;half=1;}else{printf("%u,",byte|pixel);offset++;half=0;if(offset%32==0)puts("");}
 }
 if(half){printf("%u,",byte);offset++;}
 }
 printf("};\nstatic const uint16_t unicode_%u[]={",size);
 for(unsigned i=0;i<count;i++)printf("%u,",chars[i]);
 printf("};\nstatic const lv_font_fmt_txt_glyph_dsc_t glyphs_%u[]={{0},\n",size);
 for(unsigned i=0;i<count;i++){int*d=desc[i];printf("{.bitmap_index=%d,.adv_w=%d,.box_w=%d,.box_h=%d,.ofs_x=%d,.ofs_y=%d},\n",d[0],d[1],d[2],d[3],d[4],d[5]);}
 printf("};\nstatic const lv_font_fmt_txt_cmap_t map_%u={.range_start=160,.range_length=%u,.glyph_id_start=1,.unicode_list=unicode_%u,.list_length=%u,.type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY};\n",size,chars[count-1]+1,size,count);
 printf("static const lv_font_fmt_txt_dsc_t data_%u={.glyph_bitmap=bitmap_%u,.glyph_dsc=glyphs_%u,.cmaps=&map_%u,.cmap_num=1,.bpp=4};\n",size,size,size,size);
 printf("static const lv_font_t fallback_%u={.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,.line_height=%ld,.base_line=%ld,.dsc=&data_%u};\nlv_font_t station_text_%u;\n",size,face->size->metrics.height/64,-face->size->metrics.descender/64,size,size);
 }
 puts("void station_text_fonts_init(void){");
 for(unsigned i=0;i<4;i++)printf("station_text_%u=lv_font_montserrat_%u;station_text_%u.fallback=&fallback_%u;\n",sizes[i],sizes[i],sizes[i],sizes[i]);
 puts("}");FT_Done_Face(face);FT_Done_FreeType(lib);return 0;
}
