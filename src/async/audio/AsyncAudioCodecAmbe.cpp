#include <AsyncAudioCodecAmbe.h>
#include <AsyncSerial.h>
#include <AsyncUdpSocket.h>
#include <AsyncIpAddress.h>
#include <AsyncDnsLookup.h>

#include <string>
#include <map>
#include <stdlib.h>
#include <string.h>
#include <iostream>


#include <cassert>

using namespace Async;
using namespace std;

namespace {
    /*
        Multiton pattern template. It's similar to the singleton pattern, but
        enables multiple instances through the use of keys.
        NOTE: Manual destruction must be done before program exit. Not thread-safe.

        class Foo : public Multiton<Foo> {};
        Foo &foo = Foo::getRef("foobar");
        foo.bar();
        Foo::destroyAll();
     */

    template <typename T, typename Key = map<string,string> >
    class Multiton
    {
    public:
        static void destroyAll()
        {
            for (typename map<Key, T*>::const_iterator it = instances.begin(); it != instances.end(); ++it)
                delete (*it).second;
            instances.clear();
        }

        static void destroy(const Key &key)
        {
            typename map<Key, T*>::iterator it = instances.find(key);

            if (it != instances.end()) {
                delete (*it).second;
                instances.erase(it);
            }
        }

        static T* getPtr(const Key &key)
        {
            const typename map<Key, T*>::const_iterator it = instances.find(key);

            if (it != instances.end())
                return (T*)(it->second);

            T* instance = T::create(key);
            instances[key] = instance;
            return instance;
        }

        static T& getRef(const Key &key)
        {
            return *getPtr(key);
        }

    protected:
        Multiton() {}
        ~Multiton() {}

    private:
        Multiton(const Multiton&);
        Multiton& operator=(const Multiton&);

        static map<Key, T*> instances;
    };

    template <typename T, typename Key>
    map<Key, T*> Multiton<T, Key>::instances;

    /**
     * @brief Implement shared Dv3k code here
     * (e.g. initialiation and protocol)
     */
    class AudioCodecAmbeDv3k : public AudioCodecAmbe, public Multiton<AudioCodecAmbeDv3k,AudioCodecAmbe::Options> {
    public:
        template <typename T = char>
        struct Buffer {
            Buffer(T *data = (T*) NULL, size_t length = 0) : data(data), length(length) {}
            T *data;
            size_t length;
        };

        static AudioCodecAmbeDv3k *create(const Options &options);

        virtual void init()
        {
            char DV3K_REQ_PRODID[] = {DV3K_START_BYTE, 0x00, 0x01, DV3K_TYPE_CONTROL, DV3K_CONTROL_PRODID};
            Buffer<> init_packet = Buffer<>(DV3K_REQ_PRODID,sizeof(DV3K_REQ_PRODID));
            send(init_packet);
        }
        
        virtual void prodid()
        {
          char DV3K_REQ_PRODID[] = {DV3K_START_BYTE, 0x00, 0x01, 
                                   DV3K_TYPE_CONTROL, DV3K_CONTROL_PRODID};
          Buffer<> prodid_packet = Buffer<>(DV3K_REQ_PRODID,sizeof(DV3K_REQ_PRODID));
          send(prodid_packet);
          m_state = PRODID;  /* state is requesting prod-id of Stick */
        } /* getProdId */
        
        virtual void versid()
        {
          char DV3K_REQ_VERSID[] = {DV3K_START_BYTE, 0x00, 0x01, 
                                   DV3K_TYPE_CONTROL, DV3K_CONTROL_VERSTRING};
          Buffer<> versid_packet = Buffer<>(DV3K_REQ_VERSID,sizeof(DV3K_REQ_VERSID));
          send(versid_packet);
          m_state = VERSID;  /* state is requesting version-id of Stick */
        } /* versid */
 
        /* method to prepare incoming frames from BM network to be decoded later */       
        virtual Buffer<> packForDecoding(const Buffer<> &buffer) { return buffer; }

        /* method to handle prepared encoded Data from BM network and send them to AudioSink */
        virtual Buffer<> unpackDecoded(const Buffer<> &buffer) { return buffer; }

        /* method to prepare incoming Audio frames from local RX to be encoded later */
        virtual Buffer<> packForEncoding(const Buffer<> &buffer) { assert(!"unimplemented"); return Buffer<>(); }
        virtual Buffer<> unpackEncoded(const Buffer<> &buffer) { assert(!"unimplemented"); return Buffer<>(); }

        /**
         * @brief 	Write encoded samples into the decoder
         * @param 	buf  Buffer containing encoded samples
         * @param 	size The size of the buffer
         */
        virtual void writeEncodedSamples(void *buf, int size)
        {
          const char DV3K_AMBE_HEADERFRAME[] = {DV3K_START_BYTE, 0x00, 0x0c, DV3K_TYPE_AMBE, 0x01, 0x48};
          const int DV3K_AMBE_HEADERFRAME_LEN = 6;
          const int AMBE_BYTE_LEN = 9;

          char ambe_to_dv3k[16];
          Buffer<> b;
          Buffer<>ambe_frame;
          b.data = (char*)buf;
          b.length = (size_t)size;
          Buffer<> buffer = packForDecoding(b);

           // devide the 27 bytes into a 9 byte long frame, sends them to the dv3k-decoder
          for (int a=0; a<=27; a+=9)
          {
            memcpy(ambe_to_dv3k, DV3K_AMBE_HEADERFRAME, DV3K_AMBE_HEADERFRAME_LEN);
            memcpy(ambe_to_dv3k + DV3K_AMBE_HEADERFRAME_LEN, buffer.data+a, AMBE_BYTE_LEN);
            ambe_frame = Buffer<>(ambe_to_dv3k, sizeof(ambe_to_dv3k));
            // sending HEADER + 9 bytes to DV3K-Adapter
            send(ambe_frame);
          }
        }

        virtual void send(const Buffer<> &packet) = 0;

        virtual void callback(const Buffer<> &buffer)
        {
          
          uint32_t tlen = 0;
          uint32_t t = 0;
          int type = 0;
          
          Buffer<> dv3k_buffer = buffer;
          
          // check if the data buffer contain valid data
          if (buffer.length < 4)
          {
            cout << "*** ERROR: DV3k frame to short, re-init." << endl;
            init();
            return;
          }
          
          if (buffer.data[0] == DV3K_START_BYTE)
          {
            tlen = buffer.data[2] + buffer.data[1] * 256;
            type = buffer.data[3];
            
             // is it only a part of the 160byte audio frame
            if (tlen + DV3K_HEADER_LEN > buffer.length)
            {
              stored_bufferlen = tlen; /* store the number of data (chars) in frame
                                       sent in the dv3k protocol, length ist defined in position 1 and 2.
                                       In most cases the frame received by the callback 
                                       (sigc, charactersReceived method) is only a part of the datastream 
                                       so we must concat them when all data defined in stored_bufferlen 
                                       has been reeived */
              t_buffer = Buffer<>(buffer.data, buffer.length); /* try to store the partially received
                                                                data into the temporary buffer. It shall collect
                                                                all further data, concat with each callup of
                                                                callback method */

              cout << "1st" << endl;
              for (t=0; t<t_buffer.length; t++)
              { 
                cout << hex << t_buffer.data[t];
              }                             /* shows that buffer and t_buffer are identical */
              cout << endl;
              return;
            }
            else 
            {
              stored_bufferlen = 0;
              t_buffer = Buffer<>(buffer.data, buffer.length);
            }
          }
          else if (stored_bufferlen > 0)
          {
            cout << "2nd/3rd callup:" << endl;
            for (t=0; t < t_buffer.length; t++)
            {
              cout << hex << t_buffer.data[t]; /* the t_buffer data from 1st callup has been lost :/ */
            }
            cout << endl;
            memcpy(t_buffer.data + t_buffer.length, buffer.data, buffer.length);
            t_buffer.length += buffer.length;
                  
            t_b = t_buffer.length;

            /* decoded audio frames are 160 bytes long / 164 baytes with header
               cut after 164 bytes and send them to the 
               packForDecoder + AudioDecoder::sinkWriteSamples */
            if (t_buffer.length >= stored_bufferlen + DV3K_HEADER_LEN)
            {
              // prepare a complete frame to be handled by following methods
              cout << "stored_bufferlen=" << stored_bufferlen << ", t_buffer.len=" 
                << t_buffer.length << endl;
              memcpy(dv3k_buffer.data, t_buffer.data, stored_bufferlen + DV3K_HEADER_LEN);
              dv3k_buffer.length = stored_bufferlen + DV3K_HEADER_LEN;
              

              // move the rest of the buffer in t_buffer to the begin
              memmove(t_buffer.data, t_buffer.data + (stored_bufferlen + DV3K_HEADER_LEN), 
                        t_buffer.length - stored_bufferlen - DV3K_HEADER_LEN);
        
              t_buffer.length = t_b - stored_bufferlen - DV3K_HEADER_LEN;
              t_buffer.data[t_buffer.length - stored_bufferlen - DV3K_HEADER_LEN + 1] = '\0';
            }
            else return;
          } 
          else
          {
             return;
          }

          tlen = dv3k_buffer.data[2] + dv3k_buffer.data[1] * 256;
          type = dv3k_buffer.data[3];
          
          uint32_t a;

          /* test the type of incoming frame */
          if (type == DV3K_TYPE_CONTROL)
          {
            if (m_state == RESET)
            {
              cout << "--- Device: Reset OK" << endl;
              prodid();
            }
            else if (m_state == PRODID)
            {
              m_state = VERSID;          /* give out product id of DV3k */
              cout << "--- Device: ";
              for (a=5; a < dv3k_buffer.length; a++)
              {
                cout << hex << dv3k_buffer.data[a];
              } 
              cout << endl;
              versid();
            }
            else if (m_state == VERSID)
            {
              m_state = READY;            /* give out version of DV3k */
              cout << "--- Device: " << endl;
              for (a=5; a < dv3k_buffer.length; a++)
              {
                cout << hex << dv3k_buffer.data[a];
              }
              cout << endl;
            }
          }
          /* test if buffer contains encoded frame (AMBE) */
          else if (type == DV3K_TYPE_AMBE)
          {
            // prepare encoded Frames to be send to BM network
            Buffer<> unpacked = unpackEncoded(dv3k_buffer);
            
            // forward encoded samples
            AudioEncoder::writeEncodedSamples(unpacked.data, unpacked.length);                        
          }
          else if (type == DV3K_TYPE_AUDIO)
          {
            // unpack decoded frame
            Buffer<> unpacked = unpackDecoded(dv3k_buffer);
            // pass decoded samples into sink
            AudioDecoder::sinkWriteSamples((float *)unpacked.data, unpacked.length);
          }
          else
          {
            cout << "--- WARNING: received unkown DV3K type." << endl;
          }
        }

        virtual int writeSamples(const float *samples, int count)
        {
          assert(!"unimplemented");
          Buffer<> packet = packForEncoding(Buffer<>((char*)samples,count));
          send(packet);
          return count;
        }

    protected:
      static const char DV3K_TYPE_CONTROL = 0x00;
      static const char DV3K_TYPE_AMBE = 0x01;
      static const char DV3K_TYPE_AUDIO = 0x02;
      static const char DV3K_HEADER_LEN = 0x04;
      
      static const char DV3K_START_BYTE = 0x61;

      static const char DV3K_CONTROL_RATEP  = 0x0A;
      static const char DV3K_CONTROL_PRODID = 0x30;
      static const char DV3K_CONTROL_VERSTRING = 0x31;
      static const char DV3K_CONTROL_RESET = 0x33;
      static const char DV3K_CONTROL_READY = 0x39;
      static const char DV3K_CONTROL_CHANFMT = 0x15;

      /**
      * @brief 	Default constuctor
      */
      //AudioCodecAmbeDv3k(void) : device_initialized(false) {}
      AudioCodecAmbeDv3k(void) : m_state(OFFLINE) {}

    private:
      enum STATE {OFFLINE, RESET, INIT, PRODID, VERSID, READY, WARNING, ERROR};
      STATE m_state;
      uint32_t stored_bufferlen;
      int act_framelen;
      Buffer<> t_buffer;
      int t_b;

      AudioCodecAmbeDv3k(const AudioCodecAmbeDv3k&);
      AudioCodecAmbeDv3k& operator=(const AudioCodecAmbeDv3k&);
    };

    /**
     * TODO: Implement communication with Dv3k via UDP here.
     */
    class AudioCodecAmbeDv3kAmbeServer : public AudioCodecAmbeDv3k {
    public:
      /**
      * @brief  Default constuctor
      *         TODO: parse options for IP and PORT
      */
      AudioCodecAmbeDv3kAmbeServer(const Options &options) 
      {
        Options::const_iterator it;

        if ((it=options.find("AMBESERVER_HOST"))!=options.end())
        {
          ambehost = (*it).second;
        }
        else
        {
          throw "*** ERROR: Parameter AMBESERVER_HOST not defined.";
        }

        if((it=options.find("AMBESERVER_PORT"))!=options.end())
        {
          ambeport = atoi((*it).second.c_str());
        }
        else
        {
          throw "*** ERROR: Parameter AMBESERVER_PORT not defined.";
        }
        udpInit();
      }

      /* initialize the udp socket */
      void udpInit(void)
      {
        ambesock = new UdpSocket(ambeport);
        ambesock->dataReceived.connect(mem_fun(*this, &AudioCodecAmbeDv3kAmbeServer::callbackUdp));

        if (ip_addr.isEmpty())
        {
          dns = new DnsLookup(ambehost);
          dns->resultsReady.connect(mem_fun(*this,
                &AudioCodecAmbeDv3kAmbeServer::dnsResultsReady));
          return;
        }

        init();
      } /* udpInit */
      
      /* called-up when dns has been resolved */
      void dnsResultsReady(DnsLookup& dns_lookup)
      {
        vector<IpAddress> result = dns->addresses();

        delete dns;
        dns = 0;
        if (result.empty() || result[0].isEmpty())
        {
          ip_addr.clear();
          return;
        }
        ip_addr = result[0];
      } /* dnsResultReady */

      virtual void send(const Buffer<> &packet) 
      {
        ambesock->write(ambehost, ambeport, packet.data, packet.length);
      }
      
      ~AudioCodecAmbeDv3kAmbeServer()
      {
        delete dns;
        dns = 0;
        delete ambesock;
      }
   
   protected:
      virtual void callbackUdp(const IpAddress& addr, uint16_t port,
                                         void *buf, int count)
      {
        callback(Buffer<>(static_cast<char *>(buf),count));
      }
    
    private:

      int ambeport;
      string ambehost;
      UdpSocket * ambesock;
      IpAddress	ip_addr;
      DnsLookup	*dns;

      AudioCodecAmbeDv3kAmbeServer(const AudioCodecAmbeDv3kAmbeServer&);
      AudioCodecAmbeDv3kAmbeServer& operator=(const AudioCodecAmbeDv3kAmbeServer&);
    };

    /**
     * TODO: Implement communication with Dv3k via TTY here.
     */
    class AudioCodecAmbeDv3kTty : public AudioCodecAmbeDv3k {
    public:
      /**
      * @brief 	Default constuctor
      */
      AudioCodecAmbeDv3kTty(const Options &options) {
        Options::const_iterator it;
        int baudrate;
        string device;

        if ((it=options.find("TTY_DEVICE"))!=options.end())
        {
          device = (*it).second;
        }
        else
        {
          throw "*** ERROR: Parameter AMBE_TTY_DEVICE not defined.";
        }

        if((it=options.find("TTY_BAUDRATE"))!=options.end())
        {
          baudrate = atoi((*it).second.c_str());
        }
        else
        {
          throw "*** ERROR: Parameter AMBE_TTY_BAUDRATE not defined.";
        }

        if (baudrate != 230400 && baudrate != 460800)
        {
          throw "*** ERROR: AMBE_TTY_BAUDRATE must be 230400 or 460800.";
        }

        serial = new Serial(device);
        serial->setParams(baudrate, Serial::PARITY_NONE, 8, 1, Serial::FLOW_NONE);
        if (!(serial->open(true)))
        {
          cerr << "*** ERROR: Can not open device " << device << endl;
          throw;
        }
        serial->charactersReceived.connect(
             sigc::mem_fun(*this, &AudioCodecAmbeDv3kTty::callbackTty));
        init();      
      }

      virtual void send(const Buffer<> &packet) {
        serial->write(packet.data, packet.length);
      }

      ~AudioCodecAmbeDv3kTty()
      {
          serial->close();
          delete serial;
      }

    protected:
      virtual void callbackTty(const char *buf, int count)
      {
        callback(Buffer<>(const_cast<char *>(buf),count));
      }

    private:
      Serial *serial;

      AudioCodecAmbeDv3kTty(const AudioCodecAmbeDv3kTty&);
      AudioCodecAmbeDv3kTty& operator=(const AudioCodecAmbeDv3kTty&);
    };

    AudioCodecAmbeDv3k *AudioCodecAmbeDv3k::create(const Options &options) {
        Options::const_iterator type_it = options.find("TYPE");
        if(type_it!=options.end())
        {
            if(type_it->second=="AMBESERVER")
                return new AudioCodecAmbeDv3kAmbeServer(options);
            else if(type_it->second=="TTY")
                return new AudioCodecAmbeDv3kTty(options);
            else
                throw "unknown Ambe codec TYPE";
        }
        else
            throw "unspecified Ambe codec TYPE";
    }
}


AudioCodecAmbe *AudioCodecAmbe::create(const Options &options) {
    Options::const_iterator type_it = options.find("TYPE");
    if(type_it!=options.end())
    {
        if(type_it->second=="AMBESERVER" || type_it->second=="TTY")
            return AudioCodecAmbeDv3k::getPtr(options);
        else
            throw "unknown Ambe codec TYPE";
    }
    else
        throw "unspecified Ambe codec TYPE";
}
