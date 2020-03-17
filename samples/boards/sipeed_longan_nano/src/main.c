/*
 * Copyright (c) 2019 Tavish Naruka <tavishnaruka@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Sample which uses the filesystem API and SDHC driver */

#include <zephyr.h>
#include <device.h>
#include <disk/disk_access.h>
#include <fs/fs.h>
#include <ff.h>
#include <display.h>

//LOG_MODULE_REGISTER(main);

static int lsdir(const char *path);

static FATFS fat_fs;
/* mounting info */
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
};

/*
*  Note the fatfs library is able to mount only strings inside _VOLUME_STRS
*  in ffconf.h
*/
static const char *disk_mount_pt = "/SD:";

static void backlight_init(void)
{
    /* If you have a backlight, set it up and turn it on here */
}

void main(void)
{
	//LOG_INF("ST7735S display sample");
	struct device *display_dev;

	struct display_capabilities capabilities;
	struct display_buffer_descriptor buf_desc;

	backlight_init();

#ifdef CONFIG_ST7735S_RGB565
	const size_t rgb_size = 2;
#else
	const size_t rgb_size = 3;
#endif

	display_dev = device_get_binding(DT_INST_0_SITRONIX_ST7735S_LABEL);

	if (display_dev == NULL) {
		printk("Device not found. Aborting test.");
		return;
	}

	display_get_capabilities(display_dev, &capabilities);

	/* size of the rectangle */
	const size_t w = 40;
	const size_t h = 20;
	const size_t buf_size = rgb_size * w * h;

	/* points are clockwise, from top left */
	size_t x0, y0, x1, y1, x2, y2, x3, y3;

	/* top left */
	x0 = 0;
	y0 = 0;
	/* top right */
	x1 = capabilities.x_resolution - w;
	y1 = 0;
	/* bottom right */
	x2 = capabilities.x_resolution - w;
	y2 = capabilities.y_resolution - h;
	/* bottom left */
	x3 = 0;
	y3 = capabilities.y_resolution - h;

	/* Allocate rectangular buffer for corner data */
	u8_t *buf = k_malloc(buf_size);

	if (buf == NULL) {
		printk("Could not allocate memory. Aborting test.");
		return;
	}

	/* Clear frame buffer before enabling LCD, reuse corner buffer
	 */
	int h_step;
	(void)memset(buf, 0, buf_size);
	h_step = (w * h) / capabilities.x_resolution;

	buf_desc.buf_size = buf_size;
	buf_desc.pitch = capabilities.x_resolution;
	buf_desc.width = capabilities.x_resolution;
	buf_desc.height = h_step;

	for (int idx = 0; idx < capabilities.y_resolution; idx += h_step) {
		display_write(display_dev, 0, idx, &buf_desc,  buf);
	}

	display_blanking_off(display_dev);

	buf_desc.pitch = w;
	buf_desc.width = w;
	buf_desc.height = h;

	int grey_count = 0;
	size_t cnt = 0;

	/* raw disk i/o */
	do {
		static const char *disk_pdrv = "SD";
		u64_t memory_size_mb;
		u32_t block_count;
		u32_t block_size;

		if (disk_access_init(disk_pdrv) != 0) {
			printk("Storage init ERROR!");
			break;
		}

		if (disk_access_ioctl(disk_pdrv,
				DISK_IOCTL_GET_SECTOR_COUNT, &block_count)) {
			printk("Unable to get sector count");
			break;
		}
		//LOG_INF("Block count %u", block_count);

		if (disk_access_ioctl(disk_pdrv,
				DISK_IOCTL_GET_SECTOR_SIZE, &block_size)) {
			printk("Unable to get sector size");
			break;
		}
		printk("Sector size %u\n", block_size);

		memory_size_mb = (u64_t)block_count * block_size;
		printk("Memory Size(MB) %u\n", (u32_t)memory_size_mb>>20);
	} while (0);

	mp.mnt_point = disk_mount_pt;

	int res = fs_mount(&mp);

	if (res == FR_OK) {
		printk("Disk mounted.\n");
		lsdir(disk_mount_pt);
	} else {
		printk("Error mounting disk.\n");
	}

	while (1) {
		/* Update the color of the rectangle buffer and write the buffer
		 * to  one of the corners
		 */
#ifdef CONFIG_ST7735S_RGB565
		int color = cnt % 4;
		/* RGB565 format */
		u16_t color_r;
		u16_t color_g;
		u16_t color_b;
		u16_t color_rgb;

		color_r = (color == 0) ? 0xF800U : 0U;
		color_g = (color == 1) ? 0x07E0U : 0U;
		color_b = (color == 2) ? 0x001FU : 0U;
		color_rgb = color_r + color_g + color_b;
		if (color == 3) {
			u16_t t = grey_count & 0x1f;
			/* shift the green an extra bit, it has 6 bits */
			color_rgb = t << 11 | t << (5+1) | t;
			grey_count++;
		}

		for (size_t idx = 0; idx < buf_size; idx += rgb_size) {
			*(buf + idx + 0) = (color_rgb >> 8) & 0xFFU;
			*(buf + idx + 1) = (color_rgb >> 0) & 0xFFU;
		}
#else
		u32_t color_rgb;
		u32_t c = grey_count & 0xff;

		switch (cnt % 4) {
		case 0:
			color_rgb = 0x00FF0000u;
			break;
		case 1:
			color_rgb = 0x0000FF00u;
			break;
		case 2:
			color_rgb = 0x000000FFu;
			break;
		case 3:
			color_rgb = c << 16 | c << 8 | c;
			grey_count++;
			break;
		}

		/* RGB888 format */
		for (size_t idx = color; idx < buf_size; idx += rgb_size) {
			*(buf + idx + 0) = color_rgb >> 16;
			*(buf + idx + 1) = color_rgb >> 8;
			*(buf + idx + 2) = color_rgb >> 0;
		}
#endif

		switch (cnt % 4) {
		case 0:
			/* top left, red */
			display_write(display_dev, x0, y0, &buf_desc, buf);
			break;
		case 1:
			/* top right, green */
			display_write(display_dev, x1, y1, &buf_desc, buf);
			break;
		case 2:
			/* bottom right, blue */
			display_write(display_dev, x2, y2, &buf_desc, buf);
			break;
		case 3:
			/* bottom left, alternating grey */
			display_write(display_dev, x3, y3, &buf_desc, buf);
			break;
		}
		++cnt;
		k_sleep(K_MSEC(100));
		printk(".");
	}
}

static int lsdir(const char *path)
{
	int res;
	struct fs_dir_t dirp;
	static struct fs_dirent entry;

	/* Verify fs_opendir() */
	res = fs_opendir(&dirp, path);
	if (res) {
		printk("Error opening dir %s [%d]\n", path, res);
		return res;
	}

	printk("\nListing dir %s ...\n", path);
	for (;;) {
		/* Verify fs_readdir() */
		res = fs_readdir(&dirp, &entry);

		/* entry.name[0] == 0 means end-of-dir */
		if (res || entry.name[0] == 0) {
			break;
		}

		if (entry.type == FS_DIR_ENTRY_DIR) {
			printk("[DIR ] %s\n", entry.name);
		} else {
			printk("[FILE] %s (size = %zu)\n",
				entry.name, entry.size);
		}
	}

	/* Verify fs_closedir() */
	fs_closedir(&dirp);

	return res;
}
