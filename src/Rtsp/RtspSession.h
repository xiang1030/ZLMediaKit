/*
 * MIT License
 *
 * Copyright (c) 2016 xiongziliang <771730766@qq.com>
 *
 * This file is part of ZLMediaKit(https://github.com/xiongziliang/ZLMediaKit).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef SESSION_RTSPSESSION_H_
#define SESSION_RTSPSESSION_H_

#include <set>
#include <vector>
#include <unordered_map>
#include "Common/config.h"
#include "Rtsp.h"
#include "RtpBroadCaster.h"
#include "RtspMediaSource.h"
#include "Player/PlayerBase.h"
#include "Util/util.h"
#include "Util/logger.h"
#include "Network/TcpSession.h"
#include "Http/HttpRequestSplitter.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

class RtspSession;

class BufferRtp : public Buffer{
public:
    typedef std::shared_ptr<BufferRtp> Ptr;
    BufferRtp(const RtpPacket::Ptr & pkt,uint32_t offset = 0 ):_rtp(pkt),_offset(offset){}
    virtual ~BufferRtp(){}

    char *data() const override {
        return (char *)_rtp->payload + _offset;
    }
    uint32_t size() const override {
        return _rtp->length - _offset;
    }
private:
    RtpPacket::Ptr _rtp;
    uint32_t _offset;
};

class RtspSession: public TcpSession, public HttpRequestSplitter {
public:
	typedef std::shared_ptr<RtspSession> Ptr;
	typedef std::function<void(const string &realm)> onGetRealm;
    //encrypted为true是则表明是md5加密的密码，否则是明文密码
    //在请求明文密码时如果提供md5密码者则会导致认证失败
	typedef std::function<void(bool encrypted,const string &pwd_or_md5)> onAuth;

	RtspSession(const std::shared_ptr<ThreadPool> &pTh, const Socket::Ptr &pSock);
	virtual ~RtspSession();
	void onRecv(const Buffer::Ptr &pBuf) override;
	void onError(const SockException &err) override;
	void onManager() override;

protected:
	//HttpRequestSplitter override
	int64_t onRecvHeader(const char *data,uint64_t len) override ;
private:
	void inputRtspOrRtcp(const char *data,uint64_t len);
	int send(const Buffer::Ptr &pkt) override{
        _ui64TotalBytes += pkt->size();
        return _pSender->send(pkt,_flags);
	}
	void shutdown() override ;
	void shutdown_l(bool close);
	bool handleReq_Options(); //处理options方法
	bool handleReq_Describe(); //处理describe方法
	bool handleReq_Setup(); //处理setup方法
	bool handleReq_Play(); //处理play方法
	bool handleReq_Pause(); //处理pause方法
	bool handleReq_Teardown(); //处理teardown方法
	bool handleReq_Get(); //处理Get方法
	bool handleReq_Post(); //处理Post方法
	bool handleReq_SET_PARAMETER(); //处理SET_PARAMETER方法

	void inline send_StreamNotFound(); //rtsp资源未找到
	void inline send_UnsupportedTransport(); //不支持的传输模式
	void inline send_SessionNotFound(); //会话id错误
	void inline send_NotAcceptable(); //rtsp同时播放数限制
	inline bool findStream(); //根据rtsp url查找 MediaSource实例
    inline void findStream(const function<void(bool)> &cb); //根据rtsp url查找 MediaSource实例

	inline void initSender(const std::shared_ptr<RtspSession> &pSession); //处理rtsp over http，quicktime使用的
	inline void sendRtpPacket(const RtpPacket::Ptr &pkt);
	inline string printSSRC(uint32_t ui32Ssrc) {
		char tmp[9] = { 0 };
		ui32Ssrc = htonl(ui32Ssrc);
		uint8_t *pSsrc = (uint8_t *) &ui32Ssrc;
		for (int i = 0; i < 4; i++) {
			sprintf(tmp + 2 * i, "%02X", pSsrc[i]);
		}
		return tmp;
	}
	inline int getTrackIndexByTrackType(TrackType type) {
		for (unsigned int i = 0; i < _aTrackInfo.size(); i++) {
			if (type == _aTrackInfo[i]->_type) {
				return i;
			}
		}
		return -1;
	}
    inline int getTrackIndexByControlSuffix(const string &controlSuffix) {
        for (unsigned int i = 0; i < _aTrackInfo.size(); i++) {
            if (controlSuffix == _aTrackInfo[i]->_control_surffix) {
                return i;
            }
        }
        return -1;
    }

	inline void onRcvPeerUdpData(int iTrackIdx, const Buffer::Ptr &pBuf, const struct sockaddr &addr);
	inline void startListenPeerUdpData();

    //认证相关
    static void onAuthSuccess(const weak_ptr<RtspSession> &weakSelf);
    static void onAuthFailed(const weak_ptr<RtspSession> &weakSelf,const string &realm);
    static void onAuthUser(const weak_ptr<RtspSession> &weakSelf,const string &realm,const string &authorization);
    static void onAuthBasic(const weak_ptr<RtspSession> &weakSelf,const string &realm,const string &strBase64);
    static void onAuthDigest(const weak_ptr<RtspSession> &weakSelf,const string &realm,const string &strMd5);

    void doDelay(int delaySec,const std::function<void()> &fun);
    void cancelDelyaTask();
private:
	char *_pcBuf = nullptr;
	Ticker _ticker;
	Parser _parser; //rtsp解析类
	string _strUrl;
	string _strSdp;
	string _strSession;
	bool _bFirstPlay = true;
    MediaInfo _mediaInfo;
	std::weak_ptr<RtspMediaSource> _pMediaSrc;
	RingBuffer<RtpPacket::Ptr>::RingReader::Ptr _pRtpReader;

	PlayerBase::eRtpType _rtpType = PlayerBase::RTP_UDP;
	bool _bSetUped = false;
	int _iCseq = 0;

	SdpAttr _sdpAttr;
	vector<SdpTrack::Ptr> _aTrackInfo;

	bool _bGotAllPeerUdp = false;

#ifdef RTSP_SEND_RTCP
	RtcpCounter _aRtcpCnt[2]; //rtcp统计,trackid idx 为数组下标
	Ticker _aRtcpTicker[2]; //rtcp发送时间,trackid idx 为数组下标
	inline void sendRTCP();
#endif

	//RTP over UDP
	bool _abGotPeerUdp[2] = { false, false }; //获取客户端udp端口计数
	weak_ptr<Socket> _apUdpSock[2]; //发送RTP的UDP端口,trackid idx 为数组下标
	std::shared_ptr<struct sockaddr> _apPeerUdpAddr[2]; //播放器接收RTP的地址,trackid idx 为数组下标
	bool _bListenPeerUdpData = false;
	RtpBroadCaster::Ptr _pBrdcaster;

	//登录认证
    string _strNonce;

	//RTSP over HTTP
	function<void(void)> _onDestory;
	bool _bBase64need = false; //是否需要base64解码
	Socket::Ptr _pSender; //回复rtsp时走的tcp通道，供quicktime用
	//quicktime 请求rtsp会产生两次tcp连接，
	//一次发送 get 一次发送post，需要通过sessioncookie关联起来
	string _strSessionCookie;

    //消耗的总流量
    uint64_t _ui64TotalBytes = 0;

	static recursive_mutex g_mtxGetter; //对quicktime上锁保护
	static recursive_mutex g_mtxPostter; //对quicktime上锁保护
	static unordered_map<string, weak_ptr<RtspSession> > g_mapGetter;
	static unordered_map<void *, std::shared_ptr<RtspSession> > g_mapPostter;

    std::function<void()> _delayTask;
    uint32_t _iTaskTimeLine = 0;
    atomic<bool> _enableSendRtp;
};

} /* namespace mediakit */

#endif /* SESSION_RTSPSESSION_H_ */
