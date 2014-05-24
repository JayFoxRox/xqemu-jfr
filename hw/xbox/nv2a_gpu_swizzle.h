/*
 * QEMU Geforce NV2A GPU swizzle routines
 *
 * Copyright (c) 2012 espes
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

//FIXME: Cleanup!


// Original stuff from espes - probably from cxbx or something
static void unswizzle_rect(
    uint8_t *src_buf,
    unsigned int width,
    unsigned int height,
    unsigned int depth,
    uint8_t *dst_buf,
    unsigned int pitch,
    unsigned int bytes_per_pixel)
{
    unsigned int offset_u = 0, offset_v = 0, offset_w = 0;
    uint32_t mask_u = 0, mask_v = 0, mask_w = 0;

    unsigned int i = 1, j = 1;

    while( (i <= width) || (i <= height) || (i <= depth) ) {
        if(i < width) {
            mask_u |= j;
            j<<=1;
        }
        if(i < height) {
            mask_v |= j;
            j<<=1;
        }
        if(i < depth) {
            mask_w |= j;
            j<<=1;
        }
        i<<=1;
    }

    uint32_t start_u = 0;
    uint32_t start_v = 0;
    uint32_t start_w = 0;
    uint32_t mask_max = 0;

    // get the biggest mask
    if(mask_u > mask_v)
        mask_max = mask_u;
    else
        mask_max = mask_v;
    if(mask_w > mask_max)
        mask_max = mask_w;

    for(i = 1; i <= mask_max; i<<=1) {
        if(i<=mask_u) {
            if(mask_u & i) start_u |= (offset_u & i);
            else offset_u <<= 1;
        }

        if(i <= mask_v) {
            if(mask_v & i) start_v |= (offset_v & i);
            else offset_v<<=1;
        }

        if(i <= mask_w) {
            if(mask_w & i) start_w |= (offset_w & i);
            else offset_w <<= 1;
        }
    }

    uint32_t w = start_w;
    unsigned int z;
    for(z=0; z<depth; z++) {
        uint32_t v = start_v;

        unsigned int y;
        for(y=0; y<height; y++) {
            uint32_t u = start_u;

            unsigned int x;
            for (x=0; x<width; x++) {
                memcpy(dst_buf,
                       src_buf + ( (u|v|w)*bytes_per_pixel ),
                       bytes_per_pixel);
                dst_buf += bytes_per_pixel;

                u = (u - mask_u) & mask_u;
            }
            dst_buf += pitch - width * bytes_per_pixel;

            v = (v - mask_v) & mask_v;
        }
        w = (w - mask_w) & mask_w;
    }
}


// From xbmc? Found via google..

static void Swizzle(const void *src, unsigned int depth, unsigned int width, unsigned int height, void *dest)
{
  for (unsigned int y = 0; y < height; y++)
  {
    unsigned int sy = 0;
    if (y < width)
    {
      for (int bit = 0; bit < 16; bit++)
        sy |= ((y >> bit) & 1) << (2*bit);
      sy <<= 1; // y counts twice
    }
    else
    {
      unsigned int y_mask = y % width;
      for (int bit = 0; bit < 16; bit++)
        sy |= ((y_mask >> bit) & 1) << (2*bit);
      sy <<= 1; // y counts twice
      sy += (y / width) * width * width;
    }
    uint8_t *s = (uint8_t *)src + y * width * depth;
    for (unsigned int x = 0; x < width; x++)
    {
      unsigned int sx = 0;
      if (x < height * 2)
      {
        for (int bit = 0; bit < 16; bit++)
          sx |= ((x >> bit) & 1) << (2*bit);
      }
      else
      {
        int x_mask = x % (2*height);
        for (int bit = 0; bit < 16; bit++)
          sx |= ((x_mask >> bit) & 1) << (2*bit);
        sx += (x / (2 * height)) * 2 * height * height;
      }
      uint8_t *d = (uint8_t *)dest + (sx + sy)*depth;
      for (unsigned int i = 0; i < depth; ++i)
        *d++ = *s++;
    }
  }
}


static void swizzle_rect(void* pSource, unsigned int Pitch, void* pDest, unsigned int Width, unsigned int Height, unsigned int depth, unsigned int BytesPerPixel)
{
  // knows nothing about Pitch and depth
  assert((BytesPerPixel == 1) || (BytesPerPixel == 4));
  Swizzle(pSource, BytesPerPixel, Width, Height, pDest);
}
