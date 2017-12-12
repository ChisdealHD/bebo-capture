#include "DShowCapture.h"

#include <initguid.h>
#include <streams.h>	// for DirectShow headers
#include <wmcodecdsp.h>	// for MEDIASUBTYPE_MPEG_ADTS_AAC
#include <mmreg.h>		// for WAVE_FORMAT_MPEG_ADTS_AAC
#include <dvdmedia.h>	// for VIDEOINFOHEADER2
#include <bdaiface.h>	// for IMPEG2PIDMap
#include "logging.h"
#include <libyuv/convert.h>

#include "IVideoCaptureFilterTypes.h"
#include "IVideoCaptureFilter.h"

#define RETURN_ON_FAILED(hr, message, ...) \
 { \
	info("%S:%d %d", __func__, __LINE__, hr);\
	if (FAILED(hr)) { \
		error(message,  ##__VA_ARGS__ ); \
		return; \
	} \
 }

DShowCapture::DShowCapture():
	sink_filter_(new SinkFilter(this)),
	sink_input_pin_(NULL),
	initialized(false),
    graph(NULL),
    builder(NULL),
    media_control_(NULL),
    device_filter_(NULL),
    device_output_pin_(NULL)
{
	sink_input_pin_ = sink_filter_->GetPin(0);
}

DShowCapture::~DShowCapture() {
    // FIXME release

}

#if 1

namespace pmt_log {

	int DisplayRECT(wchar_t *buffer, size_t count, const RECT& rc)
	{
		return _snwprintf(buffer, count, L"%dx%d[%d:%d]",
			rc.right - rc.left,
			rc.top - rc.bottom,
			rc.right,
			rc.bottom);
	}

	int DisplayBITMAPINFO(wchar_t *buffer, size_t count, const BITMAPINFOHEADER* pbmi)
	{
		if (pbmi->biCompression < 256) {
			return _snwprintf(buffer, count, L"[bitmap: %dx%dx%d bit  (%d)] size:%d (%d/%d)",
				pbmi->biWidth,
				pbmi->biHeight,
				pbmi->biBitCount,
				pbmi->biCompression,
				pbmi->biSizeImage,
				pbmi->biPlanes,
				pbmi->biClrUsed);
		}
		else {
			// TOOD cant test the biCompression oddity and compiler complains
			//return snprintf(buffer, count, "[bitmap: %dx%dx%d bit '%4.4hx' size:%d (%d/%d)",
			return _snwprintf(buffer, count, L"[bitmap: %dx%dx%d bit '?' size:%d (%d/%d)",
				pbmi->biWidth,
				pbmi->biHeight,
				pbmi->biBitCount,
				//&pbmi->biCompression,
				pbmi->biSizeImage,
				pbmi->biPlanes,
				pbmi->biClrUsed);
		}
	}


	int sprintf_pmt(wchar_t *buffer, size_t count, char *label, const AM_MEDIA_TYPE *pmtIn)
	{
		int cnt = 0;

		cnt += _snwprintf(&buffer[cnt], count - cnt, L"%S -", label);

		wchar_t * temporalCompression = (pmtIn->bTemporalCompression) ? L"Temporally compressed" : L"Not temporally compressed";
		cnt += _snwprintf(&buffer[cnt], count - cnt, L" [%s]", temporalCompression);

		if (pmtIn->bFixedSizeSamples) {
			cnt += _snwprintf(&buffer[cnt], count - cnt, L" [Sample Size %d]", pmtIn->lSampleSize);
		}
		else {
			cnt += _snwprintf(&buffer[cnt], count - cnt, L" [Variable size samples]");
		}

		WCHAR major_uuid[64];
		WCHAR sub_uuid[64];
		StringFromGUID2(pmtIn->majortype, major_uuid, 64);
		StringFromGUID2(pmtIn->subtype, sub_uuid, 64);

		cnt += _snwprintf(&buffer[cnt], count - cnt, L" [%s/%s]",
			major_uuid, sub_uuid);

		if (pmtIn->formattype == FORMAT_VideoInfo) {

			VIDEOINFOHEADER *pVideoInfo = (VIDEOINFOHEADER *)pmtIn->pbFormat;

			cnt += _snwprintf(&buffer[cnt], count - cnt, L" srcRect:");
			cnt += DisplayRECT(&buffer[cnt], count - cnt, pVideoInfo->rcSource);
			cnt += _snwprintf(&buffer[cnt], count - cnt, L" dstRect:");
			cnt += DisplayRECT(&buffer[cnt], count - cnt, pVideoInfo->rcTarget);
			cnt += DisplayBITMAPINFO(&buffer[cnt], count - cnt, HEADER(pmtIn->pbFormat));

		}
		else if (pmtIn->formattype == FORMAT_VideoInfo2) {

			VIDEOINFOHEADER2 *pVideoInfo2 = (VIDEOINFOHEADER2 *)pmtIn->pbFormat;

			cnt += _snwprintf(&buffer[cnt], count - cnt, L" srcRect:");
			cnt += DisplayRECT(&buffer[cnt], count - cnt, pVideoInfo2->rcSource);
			cnt += _snwprintf(&buffer[cnt], count - cnt, L" dstRect:");
			cnt += DisplayRECT(&buffer[cnt], count - cnt, pVideoInfo2->rcTarget);
			cnt += DisplayBITMAPINFO(&buffer[cnt], count - cnt, &pVideoInfo2->bmiHeader);
		}
		else {
			WCHAR format_uuid[64];
			StringFromGUID2(pmtIn->formattype, format_uuid, 64);
			cnt += _snwprintf(&buffer[cnt], count - cnt, L" Format type %ls", format_uuid);
		}
		return cnt;
	}

	void debug_pmt(char* label, const AM_MEDIA_TYPE *pmtIn)
	{
        const int SIZE = 10 * 4096;
        wchar_t buffer[SIZE];
        sprintf_pmt(buffer, SIZE, label, pmtIn);
        debug("%ls", buffer);
    }
}

#endif

void DShowCapture::QueryCapabilities() {}
void DShowCapture::EnumFilters() {}

void DShowCapture::CreateFilterGraph() {
    HRESULT hr;

    hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
        IID_IFilterGraph, (void**)&graph);
    RETURN_ON_FAILED(hr, "CoCreateInstance IID_IFilterGraph failed. hr: %d", hr);

    hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, NULL,
        CLSCTX_INPROC_SERVER,
        IID_ICaptureGraphBuilder2, (void**)&builder);
    RETURN_ON_FAILED(hr, "CoCreateInstance IID_ICaptureGraphBuilder2 failed. hr: %d", hr);

    hr = builder->SetFiltergraph(graph);
    RETURN_ON_FAILED(hr, "Failed to setFilterGraph on GraphBuilder. hr: %d", hr);

    hr = graph->QueryInterface(IID_IMediaControl, (void**)&media_control_);
    RETURN_ON_FAILED(hr, "Failed to QueryInterface IID_IMediaControl. hr: %d", hr);
}

void DShowCapture::AddDeviceFilter(GUID device_guid) {
    HRESULT hr = CoCreateInstance(device_guid, NULL, CLSCTX_INPROC, IID_IBaseFilter, (void **)&device_filter_);
    RETURN_ON_FAILED(hr, "Failed to create device filter. hr: %d", hr);

    hr = graph->AddFilter(device_filter_, L"Elgato Game Capture HD");
    RETURN_ON_FAILED(hr, "Failed to add device filter. hr: %d", hr);

    hr = builder->GetFiltergraph(&graph);
    RETURN_ON_FAILED(hr, "Failed to set filter graph. hr: %d", hr);

    hr = builder->FindPin(device_filter_, PINDIR_OUTPUT, &PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, TRUE, 0, &device_output_pin_);
    RETURN_ON_FAILED(hr, "Failed to find video capture output pin. hr: %d", hr);

    ComPtr<IAMStreamConfig> stream_config;
    hr = device_output_pin_->QueryInterface(stream_config.GetAddressOf());
    RETURN_ON_FAILED(hr, "Failed to query IAMStreamConfig interface. hr: %d", hr);

    int count = 0, size = 0;
    hr = stream_config->GetNumberOfCapabilities(&count, &size);
    RETURN_ON_FAILED(hr, "Failed to get number of capabilities. hr: %d", hr);

    std::unique_ptr<BYTE[]> caps(new BYTE[size]);

    int capability_index = 1;
    for (int i = 0; i < count; i++) {
        AM_MEDIA_TYPE *media_type;
        HRESULT hr = stream_config->GetStreamCaps(i, &media_type, caps.get());
        if (hr != S_OK) {
            error("HR:%d, GetStreamCaps != S_OK", hr);
            continue;
        }

        pmt_log::debug_pmt("GetStreamCaps", media_type);
        DeleteMediaType(media_type);
    }

    // TODO: Make a function to find the best match capability
    AM_MEDIA_TYPE *format;
    hr = stream_config->GetStreamCaps(capability_index, &format, caps.get());
    RETURN_ON_FAILED(hr, "Failed to get specific device caps. index: %d, hr: %d", capability_index, hr);

#if 0
    VIDEOINFOHEADER *vih = reinterpret_cast<VIDEOINFOHEADER*>(format->pbFormat);
    BITMAPINFOHEADER *bmih = NULL;
    if (format->formattype == FORMAT_VideoInfo) {
        bmih = &reinterpret_cast<VIDEOINFOHEADER*>(format->pbFormat)->bmiHeader;
    }
    else {
        bmih = &reinterpret_cast<VIDEOINFOHEADER2*>(format->pbFormat)->bmiHeader;
    }
    vih->AvgTimePerFrame = UNITS / 30;
    bmih->biWidth = 1280;
    bmih->biHeight = 720;
    bmih->biSizeImage = 1280 * 720 * (bmih->biBitCount >> 3);
#endif

    pmt_log::debug_pmt("setting device format", format);

    hr = stream_config->SetFormat(format); // TODO: This sometimes return S_FALSE
    RETURN_ON_FAILED(hr, "Failed to set device format. hr: %d", hr);

#if 0

    ComPtr<IAMBufferNegotiation> buffer_negotiation;
    hr = device_output_pin_->QueryInterface(buffer_negotiation.GetAddressOf());
    RETURN_ON_FAILED(hr, "Failed to query IAMBufferNegotiation interface. hr: %d", hr);
    ALLOCATOR_PROPERTIES allocator_properties;
    memset(&allocator_properties, 0, sizeof(allocator_properties));
    buffer_negotiation->GetAllocatorProperties(&allocator_properties);
    info("current allocator Properties count:%d", allocator_properties.cBuffers);

    if (allocator_properties.cBuffers < 2) {
        allocator_properties.cBuffers = 5;
        hr = buffer_negotiation->SuggestAllocatorProperties(&allocator_properties);
        if (hr != S_OK) {
            warn("SuggestAllocatorProperties returned %d", hr);
        }
    }
#endif

    DeleteMediaType(format);
    info("Finished adding device filter. HR: %d", hr);
}

void DShowCapture::AddSinkFilter() {
    HRESULT hr = graph->AddFilter(sink_filter_, NULL);
    RETURN_ON_FAILED(hr, "Failed to find video capture output pin. hr: %d", hr);

    hr = graph->ConnectDirect(device_output_pin_, sink_input_pin_, NULL);
    RETURN_ON_FAILED(hr, "Failed to find video capture output pin. hr: %d", hr);

    info("Finished adding sink filter. HR: %d", hr);
}


bool DShowCapture::Initialize() {
    if (initialized) return true;

    initialized = true;
    CreateFilterGraph();
    AddDeviceFilter(CLSID_ElgatoVideoCaptureFilter);
    AddSinkFilter();
    Run();
    return true;
}

void DShowCapture::Run() {
    info("DShowCapture::Run()");
    HRESULT hr = media_control_->Run();
    info("DShowCapture::Run() hr: %d", hr);
}

// FillBuffer - Blocking
bool DShowCapture::GetFrame(IMediaSample * sample)
{
    debug("GetFrame - start - queue size %d", media_sample_queue_.size());

	IMediaSample *queued_sample;
    long ms = 1000;
	while (!media_sample_queue_.wait_for_and_pop(queued_sample, ms)) {
        debug("No sample within %d ms", ms);
		return false;
	}

	debug("GetFrame - got frame from queue");

	uint8_t* out_buffer;
	sample->GetPointer(&out_buffer);

	uint8_t* queued_sample_buffer;
	queued_sample->GetPointer(&queued_sample_buffer);

	long queued_sample_size = queued_sample->GetActualDataLength();
	long out_sample_size = sample->GetSize();

	// debug("ActualDataLength: %ld, Size: %ld", queued_sample->GetActualDataLength(), queued_sample->GetSize());

	int negotiated_width = 1280;
	int negotiated_height = 720;

	int stride_uyvy = 1280 * 2;
	uint8* y = out_buffer;
	int stride_y = negotiated_width;
	uint8* u = out_buffer + (negotiated_width * negotiated_height);
	int stride_u = (negotiated_width + 1) / 2;
	uint8* v = u + ((negotiated_width * negotiated_height) >> 2);
	int stride_v = stride_u;

	libyuv::UYVYToI420(queued_sample_buffer,
		stride_uyvy,
		y,
		stride_y,
		u,
		stride_u,
		v,
		stride_v,
		negotiated_width,
		negotiated_height);
	
	/*
	if (out_sample_size < queued_sample_size) {
		error("out_sample_size < queued_sample_size. %ld < %ld", out_sample_size, queued_sample_size);
		return false;
	}		
	memcpy(out_buffer, queued_sample_buffer, out_sample_size);
	*/

	static REFERENCE_TIME last_end_time = 0;
	REFERENCE_TIME start_frame;
	REFERENCE_TIME end_frame;
	queued_sample->GetTime(&start_frame, &end_frame);

#if 0
	if (last_end_time != 0) {
		REFERENCE_TIME new_start_time = last_end_time + 1;
		//REFERENCE_TIME new_end_time = new_start_time + (end_frame - start_frame);
		sample->SetTime((REFERENCE_TIME *)&new_start_time, (REFERENCE_TIME *)&end_frame);
	    debug("start: %lld, stop: %lld, delta: %lld", new_start_time, end_frame, end_frame - new_start_time);
        last_end_time = end_frame;
	} else {
	    debug("last_end_time: %lld start: %lld, stop: %lld, delta: %lld", last_end_time, start_frame, end_frame, end_frame - start_frame);
		sample->SetTime((REFERENCE_TIME *)&start_frame, (REFERENCE_TIME *)&end_frame);
        last_end_time = end_frame;
	}
#else
	sample->SetTime((REFERENCE_TIME *)&start_frame, (REFERENCE_TIME *)&end_frame);
#endif

	sample->SetSyncPoint(TRUE);

	bool isDiscontinuity = (queued_sample->IsDiscontinuity() == S_OK);
	sample->SetDiscontinuity(isDiscontinuity);

	queued_sample->Release();

	return true;
}

// FrameObserver from Device Filter
void DShowCapture::FrameReceived(IMediaSample * sample)
{
	debug("FrameReceived");
	media_sample_queue_.push(sample);
}
