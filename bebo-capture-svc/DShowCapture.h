#pragma once

#include "sink_filter_win.h"
#include "shared_queue.h"
#include <atomic>

class DShowCapture : public SinkFilterObserver {
public:
	DShowCapture();
	~DShowCapture();

	void QueryCapabilities();
	void AddDeviceFilter(GUID device_guid);
	void AddDeviceAudioRendererFilter();
    HRESULT GetDeviceSettings();
	void CreateFilterGraph();
	void Run();

	bool Initialize();
	bool GetFrame(IMediaSample **left_hand_sample);

	void FrameReceived(IMediaSample* sample) override;

private:
	ComPtr<IGraphBuilder> graph_;
	ComPtr<ICaptureGraphBuilder2> builder_;
	ComPtr<IMediaControl> media_control_;

	ComPtr<IBaseFilter> device_filter_;
	ComPtr<IPin> device_video_output_pin_;
	ComPtr<IPin> device_audio_output_pin_;

	ComPtr<SinkFilter> audio_renderer_filter_;
	ComPtr<IPin> audio_renderer_pin_;

	ComPtr<SinkFilter> sink_filter_;
	ComPtr<IPin> sink_input_pin_;

	shared_queue<IMediaSample*> media_sample_queue_;

	bool initialized_;
};

