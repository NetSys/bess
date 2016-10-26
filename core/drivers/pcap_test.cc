#include <gtest/gtest.h>
#include <google/protobuf/util/message_differencer.h>
#include "pcap.h"

#define TEST_STRLEN 25

/*!
 * This mock LibPCAP library just records what was given to it as parameters and
 * returns whatever preset values. By default, it returns what looks like
 * successful behavior (e.g. fake pointers, no error messages, etc).
 */
class LibPCAPMock : public LibPCAP{
  public:
    LibPCAPMock(){
      //SETNONBLOCK PARAMETERS
      //Initially hasn't seen any last handle
      setnonblock_lasthandle = (pcap_t*) 0;
      //Return 0 means susccess.
      setnonblock_return = 0;

      //OPENLIVE PARAMETERS
      //Return a fake, nonzero pointer.
      openlive_return = (pcap_t*) 9001;
      //Haven't seen anything yet, set it to 0.
      memset(openlive_lastdevice, 0, TEST_STRLEN);

      //CLOSE PARAMETERS
      //Has close() ever been called?
      close_called = false;
    }
    ~LibPCAPMock(){}

    char openlive_lastdevice[TEST_STRLEN];
    pcap_t* openlive_return;
    pcap_t* setnonblock_lasthandle;
    int setnonblock_return;
    bool close_called;

    virtual pcap_t* open_live(const char *device, int snaplen, int promisc, int to_ms, char *errbuf){
      strncpy((char*) &openlive_lastdevice, device, TEST_STRLEN);
      return openlive_return ;
    }

    virtual int setnonblock(pcap_t *p, int nonblock, char *errbuf){
      setnonblock_lasthandle = p;
      return setnonblock_return;
    }

    void close(pcap_t* p){
      close_called = true;
    } //Do nothing, there's no real handle.
};

class PCAPPortTest : public ::testing::Test{
  protected:
    virtual void SetUp() {
      delete port.pcap_dev_;
      mock_pcap = new LibPCAPMock;
      port.pcap_dev_ = (LibPCAP*) mock_pcap;
    }

    // virtual void TearDown() {}

    PCAPPort port;
    LibPCAPMock* mock_pcap;
};

TEST_F(PCAPPortTest, InitSuccess) {
  //Init with fake device
  std::string fake_device = "eth9001"; //It's over 9000.
  bess::PCAPPortArg arg;
  arg.set_dev(fake_device);

  //Check some expected values.
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(pb_errno(0), port.Init(arg)));
  EXPECT_EQ(mock_pcap->openlive_return, mock_pcap->setnonblock_lasthandle);
  EXPECT_EQ(mock_pcap->openlive_lastdevice, fake_device);
}

TEST_F(PCAPPortTest, InitBadDevice) {
  //Init with fake device
  std::string fake_device = "eth9002"; //It's over 9000.
  bess::PCAPPortArg arg;
  arg.set_dev(fake_device);

  mock_pcap->openlive_return =  (pcap_t*) 0;
  //Return a null handle -- should produce an rror.

  EXPECT_EQ(ENODEV, port.Init(arg).err());
}

TEST_F(PCAPPortTest, InitCantSetNonblock) {
  //Init with fake device
  std::string fake_device = "eth9003"; //It's over 9000.
  bess::PCAPPortArg arg;
  arg.set_dev(fake_device);

  mock_pcap->setnonblock_return = 100; 
  //produce an error on setting nonblock

  EXPECT_EQ(ENODEV, port.Init(arg).err());
}

TEST_F(PCAPPortTest, DeInit){
  std::string fake_device = "eth9004"; //It's STILL over 9000.
  bess::PCAPPortArg arg;
  arg.set_dev(fake_device);
  port.Init(arg);

  //Just make sure DeInit calls close...
  port.DeInit();
  EXPECT_TRUE(mock_pcap->close_called);
}

TEST_F(PCAPPortTest, SendPacketsSinglePacket){
  queue_t quid = (queue_t) 9005; //This joke doesn't get old.
  int cnt = 1;
  struct snbuf fakepackets;
  memcpy(fakepackets, 42, sizeof(snbuf)); //Need to replace with realishd ata because pointers are followed around sometimes....
  struct snbuf* pointer_to_my_fake_packets = &fakepackets;
  snb_array_t fake_packets_array = (snb_array_t) &pointer_to_my_fake_packets;

  port.SendPackets(quid, fake_packets_array, 1);
}


int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  google::InitGoogleLogging("bess_pcap_test");
  return RUN_ALL_TESTS();
}
