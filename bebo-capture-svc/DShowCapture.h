#pragma once

#include "sink_filter_win.h"
#include "shared_queue.h"
#include <atomic>

class DShowCapture : public SinkFilterObserver {
public:
	DShowCapture();
	~DShowCapture();

	void QueryCapabilities();
	void EnumFilters();
	void AddDeviceFilter(GUID device_guid);
	void CreateFilterGraph();
	void AddSinkFilter();
	void Run();

	bool Initialize();
	bool GetFrame(IMediaSample *sample);

	void FrameReceived(IMediaSample* sample) override;

private:
	IGraphBuilder *graph;
	ICaptureGraphBuilder2 *builder;
	IMediaControl *media_control_;

	IBaseFilter *device_filter_;
	IPin *device_output_pin_;

	SinkFilter *sink_filter_;
	IPin *sink_input_pin_;
	shared_queue<IMediaSample*> media_sample_queue_;

	bool initialized;
};

