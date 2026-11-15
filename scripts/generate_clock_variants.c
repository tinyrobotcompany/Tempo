/* Build: cc scripts/generate_clock_variants.c $(pkg-config --cflags --libs freetype2) -o /tmp/station-font
 * Run: /tmp/station-font Inter.ttf symbol weight width_factor; stdout is the generated C font.
 * Rasterises only clock glyphs at native 192px, without runtime scaling/layers. */
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){
 if(argc!=5)return 1;FT_Library lib;FT_Face face;
 if(FT_Init_FreeType(&lib)||FT_New_Face(lib,argv[1],0,&face))return 2;
 FT_MM_Var *axes=NULL;
 if(FT_Get_MM_Var(face,&axes))return 2;
 FT_Fixed values[8];if(axes->num_axis>8)return 2;
 for(unsigned i=0;i<axes->num_axis;i++){
  values[i]=axes->axis[i].def;
  if(axes->axis[i].tag==FT_MAKE_TAG('w','g','h','t'))values[i]=atoi(argv[3])*65536;
  if(axes->axis[i].tag==FT_MAKE_TAG('o','p','s','z'))values[i]=32*65536;
 }
 if(FT_Set_Var_Design_Coordinates(face,axes->num_axis,values)||FT_Set_Pixel_Sizes(face,0,192))return 2;
 FT_Done_MM_Var(lib,axes);
 FT_Matrix matrix={(FT_Fixed)(atof(argv[4])*65536),0,0,65536};FT_Set_Transform(face,&matrix,NULL);
 const char*chars="-0123456789:";unsigned offset=0;unsigned desc[12][6];
 puts("/* Generated from Inter variable font at native 192px (SIL OFL 1.1). See Inter-OFL.txt. */\n#include \"lvgl.h\"\nstatic const uint8_t bitmap[]={");
 for(unsigned i=0;i<12;i++){
  if(FT_Load_Char(face,chars[i],FT_LOAD_RENDER))return 3;FT_GlyphSlot g=face->glyph;
  desc[i][0]=offset;desc[i][1]=g->advance.x/4;desc[i][2]=g->bitmap.width;desc[i][3]=g->bitmap.rows;desc[i][4]=g->bitmap_left;desc[i][5]=g->bitmap_top-g->bitmap.rows;
  for(unsigned y=0;y<g->bitmap.rows;y++)for(unsigned x=0;x<g->bitmap.width;x++){printf("%u,",g->bitmap.buffer[y*g->bitmap.pitch+x]);if(++offset%32==0)puts("");}
 }
 puts("};\nstatic const lv_font_fmt_txt_glyph_dsc_t glyphs[]={ {0},");
 for(unsigned i=0;i<12;i++)printf("{.bitmap_index=%u,.adv_w=%u,.box_w=%u,.box_h=%u,.ofs_x=%d,.ofs_y=%d},\n",desc[i][0],desc[i][1],desc[i][2],desc[i][3],(int)desc[i][4],(int)desc[i][5]);
 puts("};\nstatic const lv_font_fmt_txt_cmap_t maps[]={\n{.range_start=45,.range_length=1,.glyph_id_start=1,.type=LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},\n{.range_start=48,.range_length=11,.glyph_id_start=2,.type=LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY}\n};\nstatic const lv_font_fmt_txt_dsc_t data={.glyph_bitmap=bitmap,.glyph_dsc=glyphs,.cmaps=maps,.cmap_num=2,.bpp=8};");
 printf("const lv_font_t %s={.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,.line_height=%ld,.base_line=%ld,.dsc=&data};\n",argv[2],face->size->metrics.height/64,-face->size->metrics.descender/64);
 FT_Done_Face(face);FT_Done_FreeType(lib);return 0;
}
