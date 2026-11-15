/* Build: cc scripts/generate_clock_font.c $(pkg-config --cflags --libs freetype2) -o /tmp/station-font
 * Run with the LVGL Montserrat-Medium.ttf path; stdout is the generated C font.
 * Rasterises only clock glyphs at native 240px, without runtime scaling/layers. */
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){
 if(argc!=2)return 1;FT_Library lib;FT_Face face;
 if(FT_Init_FreeType(&lib)||FT_New_Face(lib,argv[1],0,&face)||FT_Set_Pixel_Sizes(face,0,240))return 2;
 const char*chars="-0123456789:";unsigned offset=0;unsigned desc[12][6];
 puts("/* Generated from Montserrat Medium (SIL OFL 1.1). See fonts/OFL.txt. */\n#include \"lvgl.h\"\nstatic const uint8_t bitmap[]={");
 for(unsigned i=0;i<12;i++){
  if(FT_Load_Char(face,chars[i],FT_LOAD_RENDER))return 3;FT_GlyphSlot g=face->glyph;
  desc[i][0]=offset;desc[i][1]=g->advance.x/4;desc[i][2]=g->bitmap.width;desc[i][3]=g->bitmap.rows;desc[i][4]=g->bitmap_left;desc[i][5]=g->bitmap_top-g->bitmap.rows;
  for(unsigned y=0;y<g->bitmap.rows;y++)for(unsigned x=0;x<g->bitmap.width;x++){printf("%u,",g->bitmap.buffer[y*g->bitmap.pitch+x]);if(++offset%32==0)puts("");}
 }
 puts("};\nstatic const lv_font_fmt_txt_glyph_dsc_t glyphs[]={ {0},");
 for(unsigned i=0;i<12;i++)printf("{.bitmap_index=%u,.adv_w=%u,.box_w=%u,.box_h=%u,.ofs_x=%d,.ofs_y=%d},\n",desc[i][0],desc[i][1],desc[i][2],desc[i][3],(int)desc[i][4],(int)desc[i][5]);
 puts("};\nstatic const lv_font_fmt_txt_cmap_t maps[]={\n{.range_start=45,.range_length=1,.glyph_id_start=1,.type=LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},\n{.range_start=48,.range_length=11,.glyph_id_start=2,.type=LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY}\n};\nstatic const lv_font_fmt_txt_dsc_t data={.glyph_bitmap=bitmap,.glyph_dsc=glyphs,.cmaps=maps,.cmap_num=2,.bpp=8};");
 printf("const lv_font_t station_clock_font={.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,.line_height=%ld,.base_line=%ld,.dsc=&data};\n",face->size->metrics.height/64,-face->size->metrics.descender/64);
 FT_Done_Face(face);FT_Done_FreeType(lib);return 0;
}
