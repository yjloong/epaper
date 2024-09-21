#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/delay.h>

#define DRIVER_NAME "spi_epaper"
#define EPAPER_MAX_BUSY_LOOP 10
#define BUSY 1

#define DC_CMD(epaper) gpiod_set_value(epaper->dc_gpio, 1)
#define DC_DAT(epaper) gpiod_set_value(epaper->dc_gpio, 0)
#define RESET_DOWN(epaper) gpiod_set_value(epaper->reset_gpio, 1)
#define RESET_UP(epaper)   gpiod_set_value(epaper->reset_gpio, 0)

/* E-Paper Size */
#define D_x 128
#define D_y 296

#define EPAPER_SPI_WRITE(cmd, data) do {				\
		int ret = epaper_spi_write(epaper, cmd, data, sizeof(data)); \
		if (ret)       return ret;				\
	} while(0)
	
#define EPAPER_WAIT_BUSY() do {			   \
		int ret = wait_until_not_busy(epaper); \
		if (ret) return ret;		   \
	} while(0)


struct spi_epaper {
	struct spi_device *spi;
	unsigned char *tx_buf;
	struct gpio_desc *dc_gpio;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *busy_gpio;
	u8 *hw_image;
	struct fb_info e_info;
};

#define to_epaper(info) container_of(info, struct spi_epaper, e_info)

static int wait_until_not_busy(struct spi_epaper *epaper)
{
	unsigned long status;
	int loop = 0;

	do {
		status = gpiod_get_value(epaper->busy_gpio);
		msleep(1);
		if (++loop >= EPAPER_MAX_BUSY_LOOP)
			return -EBUSY;
	} while (status);

	return 0;
}

static int epaper_spi_write(struct spi_epaper *epaper, u8 cmd, u8 *dat, unsigned dat_num)
{
	struct spi_message msg;
	struct spi_device *spi = epaper->spi;
	struct spi_transfer tx[2];
	int ret;
	char buffer[200];
	
	memset(tx, 0, sizeof(tx));
	spi_message_init(&msg);
	tx[0].tx_buf = &cmd;
	tx[0].len = 1;
	spi_message_add_tail(&tx[0], &msg);
	buffer[0] = '\0';
	if (dat_num == 1)
		snprintf(buffer, sizeof(buffer), "0x%x", dat[0]);
	else if (dat_num == 2)
		snprintf(buffer, sizeof(buffer), "0x%x 0x%x", dat[0], dat[1]);
	else if (dat_num == 3)
		snprintf(buffer, sizeof(buffer), "0x%x 0x%x 0x%x", dat[0], dat[1], dat[2]);
	else if (dat_num == 4)
		snprintf(buffer, sizeof(buffer), "0x%x 0x%x 0x%x 0x%x", dat[0], dat[1], dat[2], dat[3]);
	else if (dat_num > 4)
		snprintf(buffer, sizeof(buffer), "0x%x 0x%x 0x%x 0x%x ...[%d]", dat[0], dat[1], dat[2], dat[3], dat_num);	
	dev_err(&spi->dev, "cmd: 0x%x  data: %s", ((u8*)tx[0].tx_buf)[0], buffer);
	DC_CMD(epaper);
	ret = spi_sync(spi, &msg);
	if (ret)
		goto fail;

	DC_DAT(epaper);
	if (!dat_num)
		return 0;
	
	spi_message_init(&msg);
	tx[1].tx_buf = dat;
	tx[1].len = dat_num;
	spi_message_add_tail(&tx[1], &msg);
	ret = spi_sync(spi, &msg);
	if (ret)
		goto fail;

	return 0;
	
fail:
	dev_err(&spi->dev, "error in writing cmd: %d", cmd);
	DC_DAT(epaper);
	return ret;
}

static int epaper_init(struct spi_epaper *epaper)
{
	RESET_DOWN(epaper);
	msleep(20); // at least 10ms delay
	RESET_UP(epaper);
	msleep(20); // at least 10ms delay

	EPAPER_WAIT_BUSY();
	EPAPER_SPI_WRITE(0x12, (u8[]){}); // software reset
	EPAPER_WAIT_BUSY();
	EPAPER_SPI_WRITE(0x1, ((u8[]){0x27, 0x1, 0x1})); // driver output control

	/* Use the default value in most regs. */
	/* EPAPER_SPI_WRITE(0x11, ((u8[]){0x7})); // data entry mode */
	/* EPAPER_SPI_WRITE(0x44, ((u8[]){0x00, 0x0f})); // set Ram-X address start/end */
	/* EPAPER_SPI_WRITE(0x45, ((u8[]){0x27, 0x1, 0x0, 0x0}));// set Ram-Y address start/end */
	/* EPAPER_SPI_WRITE(0x3c, ((u8[]){0x5})); */ // borderwavefrom
	// EPAPER_SPI_WRITE(0x21, ((u8[]){0x0, 0x00})); // display update control
	// EPAPER_SPI_WRITE(0x4E, ((u8[]){0x0})); // set Ram x address count to 0
	// EPAPER_SPI_WRITE(0x4F, ((u8[]){0x27, 0x1})); // set Ram y address count to 0x199
	EPAPER_WAIT_BUSY();
	return 0;
}

/* Not Used. */
#if 0
/* Transform 128x296[0-1] into 16*296[0-255]. The latter will be writen into epaper. */
static void epaper_encoding(struct spi_epaper *epaper, u8 *src)
{
	u8 *dst = epaper->hw_image;

	for (int i = 0; i < D_x * D_y / 8; i++) {
		u8 tmp = 0;
		for (int j=0; j<8; j++) {
			tmp |= (src[i * 8 + j] > 0 ? (1 << (7-j)) : 0);
		}
		dst[i] = tmp;
	}
}

/* Revert */
static void epaper_decoding(struct spi_epaper *epaper, u8 *dst)
{
	u8 *src = epaper->hw_image;

	for (int i = 0; i < D_x * D_y / 8; i++) {
		for (int j=0; j<8; j++) {
			dst[i*8+j] = (src[i] & (1 << (7-j))) > 0 ? 0xFF : 0x0;
		}
	}
}
#endif

static int epaper_update(struct spi_epaper *epaper)
{
	epaper_spi_write(epaper, 0x24, epaper->hw_image, D_x * D_y / 8);
	EPAPER_SPI_WRITE(0x22, ((u8[]){0xf7}));
	EPAPER_SPI_WRITE(0x20, ((u8[]){}));
	EPAPER_WAIT_BUSY();
	return 0;
}

/* FrameBuffer */
static const struct fb_fix_screeninfo epaperfb_fix = {
	.id = "epaper",
	.type = FB_TYPE_PACKED_PIXELS,
	.visual = FB_VISUAL_MONO10,
	.xpanstep = 0,
	.ypanstep = 0,
	.ywrapstep = 0,
	.line_length = D_x / 8,
	.accel = FB_ACCEL_NONE,
};
static struct fb_var_screeninfo epaperfb_var = {
	.xres = 128,
	.yres = 296,
	.xres_virtual = 128,
	.yres_virtual = 296,
	.bits_per_pixel = 1,
	.grayscale = 1,
};

ssize_t epaper_write(struct fb_info *info, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	int ret;
	struct spi_epaper *epaper = to_epaper(info);
	
	ret = fb_sys_write(info, buf, count, ppos);
	if (ret <= 0)
		return ret;

	epaper_update(epaper);
	return ret;
}

static struct fb_ops epaper_ops = {
	.owner = THIS_MODULE,
	.fb_write = epaper_write, /* Data from user --> info->screen_base */
	.fb_read = fb_sys_read,
	/* .fb_mmap = fb_deferred_io_mmap, */
};

static int init_fb_init(struct device *dev, struct fb_info *info)
{
	struct spi_epaper *epaper = to_epaper(info);
	// fb_set_var(info, &epaperfb_var);
	info->var = epaperfb_var;
	info->fix = epaperfb_fix;

	info->flags = FBINFO_FLAG_DEFAULT;
	info->screen_size = D_x * D_y / 8;
	info->screen_base = epaper->hw_image;
	info->fbops = &epaper_ops;
	return register_framebuffer(info);
}

static int spi_epaper_probe(struct spi_device *spi)
{
    struct spi_epaper *epaper;

    dev_info(&spi->dev, "%s: Probing SPI epaper device\n", DRIVER_NAME);

    dev_info(&spi->dev, "chip_select: %d", spi->chip_select);
    dev_info(&spi->dev, "max_speed_hz: %d", spi->max_speed_hz);
    dev_info(&spi->dev, "bit per word: %d", spi->bits_per_word);
    dev_info(&spi->dev, "cs_gpiod: %p", spi->cs_gpiod);

    epaper = devm_kzalloc(&spi->dev, sizeof(*epaper), GFP_KERNEL);
    if (!epaper) {
        dev_err(&spi->dev, "%s: Failed to allocate memory\n", DRIVER_NAME);
        return -ENOMEM;
    }

    epaper->spi = spi;
    spi_set_drvdata(spi, epaper);

    epaper->tx_buf = devm_kmalloc(&spi->dev, 1024, GFP_KERNEL);
    if (!epaper->tx_buf) {
        dev_err(&spi->dev, "%s: Failed to allocate TX buffer\n", DRIVER_NAME);
	return -ENOMEM;
    }

    spi->max_speed_hz = 1000000;
    spi->bits_per_word = 8;
    spi->mode = SPI_MODE_0;

    epaper->hw_image = devm_kmalloc(&spi->dev, D_x * D_y / 8, GFP_KERNEL);
    if (!epaper->hw_image)
	    return -ENOMEM;
    
    epaper->dc_gpio = devm_gpiod_get(&spi->dev, "dc", GPIOD_OUT_LOW);
    if (IS_ERR(epaper->dc_gpio)) {
        dev_err(&spi->dev, "%s: Failed to get D/C# GPIO\n", DRIVER_NAME);
        return PTR_ERR(epaper->dc_gpio);
    }

    epaper->reset_gpio = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_LOW);
    if (IS_ERR(epaper->reset_gpio)) {
        dev_err(&spi->dev, "%s: Failed to get REST GPIO\n", DRIVER_NAME);
        return PTR_ERR(epaper->reset_gpio);
    }

    epaper->busy_gpio = devm_gpiod_get(&spi->dev, "busy", GPIOD_IN);
    if (IS_ERR(epaper->busy_gpio)) {
        dev_err(&spi->dev, "%s: Failed to get D/C# GPIO\n", DRIVER_NAME);
        return PTR_ERR(epaper->busy_gpio);
    }
    
    epaper_init(epaper);
    init_fb_init(&spi->dev, &epaper->e_info);
    
    dev_info(&spi->dev, "%s: SPI epaper device initialized\n", DRIVER_NAME);

    return 0;
}

static void spi_epaper_remove(struct spi_device *spi)
{
    struct spi_epaper *epaper = spi_get_drvdata(spi);
    unregister_framebuffer(&epaper->e_info);
    dev_info(&spi->dev, "%s: Removing SPI epaper device\n", DRIVER_NAME);
}

static const struct spi_device_id epaper_spi_ids[] = {
	{ "epaper", 0 },
	{},
};
MODULE_DEVICE_TABLE(spi, epaper_spi_ids);

static const struct of_device_id of_epaper_spi_match[] = {
	{ .compatible = "zjy,epaper", },
	{}
};
MODULE_DEVICE_TABLE(of, of_epaper_spi_match);

static struct spi_driver spi_epaper_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .owner = THIS_MODULE,
	.of_match_table = of_match_ptr(of_epaper_spi_match),
    },
    .probe = spi_epaper_probe,
    .remove = spi_epaper_remove,
    .id_table = epaper_spi_ids,
};

module_spi_driver(spi_epaper_driver);

MODULE_AUTHOR("Jialong Yang");
MODULE_DESCRIPTION("SPI Device Driver with GPIO Control for ZJY Epaper.");
MODULE_LICENSE("GPL");
