#include "vfl.h"
#include "util.h"

void vfl_init(vfl_device_t *_vfl)
{
	memset(_vfl, 0, sizeof(*_vfl));
}

void vfl_cleanup(vfl_device_t *_vfl)
{
}

vfl_device_t *vfl_allocate()
{
	vfl_device_t *ret = malloc(sizeof(*ret));
	vfl_init(ret);
	return ret;
}

error_t vfl_open(vfl_device_t *_vfl, nand_device_t *_dev)
{
	if(!_vfl->open)
		return 0;

	return _vfl->open(_vfl, _dev);
}

void vfl_close(vfl_device_t *_vfl)
{
	if(!_vfl->close)
		return;

	_vfl->close(_vfl);
}

error_t vfl_read_single_page(vfl_device_t *_vfl, uint32_t _page, uint8_t* buffer, uint8_t* spare,
		int empty_ok, int* refresh_page)
{
	if(!_vfl->read_single_page)
		return -1;

	return _vfl->read_single_page(_vfl, _page, buffer, spare, empty_ok, refresh_page);
}
