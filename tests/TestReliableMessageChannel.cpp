#include "Connection.h"
#include "NetworkSimulator.h"
#include "ReliableMessageChannel.h"

using namespace std;
using namespace protocol;

enum PacketType
{
    PACKET_Connection
};

enum MessageType
{
    MESSAGE_Block = 0,           // IMPORTANT: 0 is reserved for block messages
    MESSAGE_Test
};

struct TestMessage : public Message
{
    TestMessage() : Message( MESSAGE_Test )
    {
        sequence = 0;
    }

    void Serialize( Stream & stream )
    {        
        serialize_bits( stream, sequence, 16 );

        for ( int i = 0; i < sequence % 8; ++i )
        {
            int value = 0;
            serialize_bits( stream, value, 32 );
        }

        stream.Check( 0xDEADBEEF );
    }

    uint16_t sequence;
    uint32_t magic;
};

class MessageFactory : public Factory<Message>
{
public:
    MessageFactory()
    {
        Register( MESSAGE_Block, [] { return make_shared<BlockMessage>(); } );
        Register( MESSAGE_Test, [] { return make_shared<TestMessage>(); } );
    }
};

class TestChannelStructure : public ChannelStructure
{
    ReliableMessageChannelConfig m_config;

public:

    TestChannelStructure()
    {
        m_config.messageFactory = make_shared<MessageFactory>();

        AddChannel( "reliable message channel", 
                    [this] { return CreateReliableMessageChannel(); }, 
                    [this] { return CreateReliableMessageChannelData(); } );

        Lock();
    }

    shared_ptr<ReliableMessageChannel> CreateReliableMessageChannel()
    {
        return make_shared<ReliableMessageChannel>( m_config );
    }

    shared_ptr<ReliableMessageChannelData> CreateReliableMessageChannelData()
    {
        return make_shared<ReliableMessageChannelData>( m_config );
    }

    const ReliableMessageChannelConfig & GetConfig() const
    {
        return m_config;
    }
};

class PacketFactory : public Factory<Packet>
{
public:

    PacketFactory( shared_ptr<ChannelStructure> channelStructure )
    {
        Register( PACKET_Connection, [channelStructure] { return make_shared<ConnectionPacket>( PACKET_Connection, channelStructure ); } );
    }
};

void test_reliable_message_channel_messages()
{
    cout << "test_reliable_message_channel_messages" << endl;

    auto channelStructure = make_shared<TestChannelStructure>();
    auto packetFactory = make_shared<PacketFactory>( channelStructure );

    const int MaxPacketSize = 256;

    ConnectionConfig connectionConfig;
    connectionConfig.packetType = PACKET_Connection;
    connectionConfig.maxPacketSize = MaxPacketSize;
    connectionConfig.packetFactory = packetFactory;
    connectionConfig.channelStructure = channelStructure;

    Connection connection( connectionConfig );

    auto messageChannel = static_pointer_cast<ReliableMessageChannel>( connection.GetChannel( 0 ) );
    
    const int NumMessagesSent = 32;

    for ( int i = 0; i < NumMessagesSent; ++i )
    {
        auto message = make_shared<TestMessage>();
        message->sequence = i;
        messageChannel->SendMessage( message );
    }

    TimeBase timeBase;
    timeBase.deltaTime = 0.01f;

    uint64_t numMessagesReceived = 0;

    Address address( "::1" );

    NetworkSimulator simulator;
    simulator.AddState( { 1.0f, 1.0f, 90 } );

    int iteration = 0;

    while ( true )
    {  
        auto writePacket = connection.WritePacket();

        uint8_t buffer[MaxPacketSize];

        Stream writeStream( STREAM_Write, buffer, MaxPacketSize );
        writePacket->Serialize( writeStream );
        writeStream.Flush();

        Stream readStream( STREAM_Read, buffer, MaxPacketSize );
        auto readPacket = make_shared<ConnectionPacket>( PACKET_Connection, channelStructure );
        readPacket->Serialize( readStream );

        simulator.SendPacket( address, readPacket );

        simulator.Update( timeBase );

        auto packet = simulator.ReceivePacket();

        if ( packet )
            connection.ReadPacket( static_pointer_cast<ConnectionPacket>( packet ) );

        assert( connection.GetCounter( Connection::PacketsRead ) <= iteration + 1 );
        assert( connection.GetCounter( Connection::PacketsWritten ) == iteration + 1 );
        assert( connection.GetCounter( Connection::PacketsAcked ) <= iteration + 1 );

        while ( true )
        {
            auto message = messageChannel->ReceiveMessage();

            if ( !message )
                break;

            assert( message->GetId() == numMessagesReceived );
            assert( message->GetType() == MESSAGE_Test );

            auto testMessage = static_pointer_cast<TestMessage>( message );

            assert( testMessage->sequence == numMessagesReceived );

            ++numMessagesReceived;
        }

        if ( numMessagesReceived == NumMessagesSent )
            break;

        connection.Update( timeBase );

        assert( messageChannel->GetCounter( ReliableMessageChannel::MessagesSent ) == NumMessagesSent );
        assert( messageChannel->GetCounter( ReliableMessageChannel::MessagesReceived ) == numMessagesReceived );
        assert( messageChannel->GetCounter( ReliableMessageChannel::MessagesEarly ) == 0 );

        timeBase.time += timeBase.deltaTime;

        if ( messageChannel->GetCounter( ReliableMessageChannel::MessagesReceived ) == NumMessagesSent )
            break;

        iteration++;
    }
}

void test_reliable_message_channel_small_blocks()
{
    cout << "test_reliable_message_channel_small_blocks" << endl;

    auto channelStructure = make_shared<TestChannelStructure>();
    auto packetFactory = make_shared<PacketFactory>( channelStructure );
    auto messageFactory = make_shared<MessageFactory>();

    const int MaxPacketSize = 256;

    ConnectionConfig connectionConfig;
    connectionConfig.packetType = PACKET_Connection;
    connectionConfig.maxPacketSize = MaxPacketSize;
    connectionConfig.packetFactory = packetFactory;
    connectionConfig.channelStructure = channelStructure;

    Connection connection( connectionConfig );

    auto messageChannel = static_pointer_cast<ReliableMessageChannel>( connection.GetChannel( 0 ) );

    auto channelConfig = channelStructure->GetConfig(); 

    const int NumMessagesSent = channelConfig.maxSmallBlockSize;

    for ( int i = 0; i < NumMessagesSent; ++i )
    {
        auto block = make_shared<Block>( i + 1, i );
        for ( int j = 0; j < block->size(); ++j )
            (*block)[j] = ( i + j ) % 256;
        messageChannel->SendBlock( block );
    }

    TimeBase timeBase;
    timeBase.deltaTime = 0.01f;

    uint64_t numMessagesReceived = 0;

    int iteration = 0;

    Address address( "::1" );

    NetworkSimulator simulator;
    simulator.AddState( { 1.0f, 1.0f, 90 } );

    while ( true )
    {  
        auto writePacket = connection.WritePacket();

        uint8_t buffer[MaxPacketSize];

        Stream writeStream( STREAM_Write, buffer, MaxPacketSize );
        writePacket->Serialize( writeStream );
        writeStream.Flush();

        Stream readStream( STREAM_Read, buffer, MaxPacketSize );
        auto readPacket = make_shared<ConnectionPacket>( PACKET_Connection, channelStructure );
        readPacket->Serialize( readStream );

        simulator.SendPacket( address, readPacket );

        simulator.Update( timeBase );

        auto packet = simulator.ReceivePacket();

        if ( packet )
            connection.ReadPacket( static_pointer_cast<ConnectionPacket>( packet ) );

        assert( connection.GetCounter( Connection::PacketsRead ) <= iteration + 1 );
        assert( connection.GetCounter( Connection::PacketsWritten ) == iteration + 1 );
        assert( connection.GetCounter( Connection::PacketsAcked ) <= iteration + 1 );

        while ( true )
        {
            auto message = messageChannel->ReceiveMessage();

            if ( !message )
                break;

            assert( message->GetId() == numMessagesReceived );
            assert( message->GetType() == MESSAGE_Block );

            auto blockMessage = static_pointer_cast<BlockMessage>( message );

            auto block = blockMessage->GetBlock();

            assert( block->size() == numMessagesReceived + 1 );
            for ( int i = 0; i < block->size(); ++i )
                assert( (*block)[i] == ( numMessagesReceived + i ) % 256 );

            ++numMessagesReceived;
        }

        if ( numMessagesReceived == NumMessagesSent )
            break;

        connection.Update( timeBase );

        assert( messageChannel->GetCounter( ReliableMessageChannel::MessagesSent ) == NumMessagesSent );
        assert( messageChannel->GetCounter( ReliableMessageChannel::MessagesReceived ) == numMessagesReceived );
        assert( messageChannel->GetCounter( ReliableMessageChannel::MessagesEarly ) == 0 );

        timeBase.time += timeBase.deltaTime;

        if ( messageChannel->GetCounter( ReliableMessageChannel::MessagesReceived ) == NumMessagesSent )
            break;

        iteration++;
    }
}

void test_reliable_message_channel_large_blocks()
{
    cout << "test_reliable_message_channel_large_blocks" << endl;

    auto channelStructure = make_shared<TestChannelStructure>();
    auto packetFactory = make_shared<PacketFactory>( channelStructure );

    const int MaxPacketSize = 256;

    ConnectionConfig connectionConfig;
    connectionConfig.packetType = PACKET_Connection;
    connectionConfig.maxPacketSize = MaxPacketSize;
    connectionConfig.packetFactory = packetFactory;
    connectionConfig.channelStructure = channelStructure;

    Connection connection( connectionConfig );

    auto messageChannel = static_pointer_cast<ReliableMessageChannel>( connection.GetChannel( 0 ) );

    auto channelConfig = channelStructure->GetConfig(); 

    const int NumMessagesSent = 16;

    for ( int i = 0; i < NumMessagesSent; ++i )
    {
        auto block = make_shared<Block>( ( i + 1 ) * 1024 + i, 0 );
        for ( int j = 0; j < block->size(); ++j )
            (*block)[j] = ( i + j ) % 256;
        messageChannel->SendBlock( block );
    }

    TimeBase timeBase;
    timeBase.deltaTime = 0.01f;

    uint64_t numMessagesReceived = 0;

    int iteration = 0;

    Address address( "::1" );

    NetworkSimulator simulator;
    simulator.AddState( { 1.0f, 1.0f, 90 } );

    while ( true )
    {  
        auto writePacket = connection.WritePacket();

        uint8_t buffer[MaxPacketSize];

        Stream writeStream( STREAM_Write, buffer, MaxPacketSize );
        writePacket->Serialize( writeStream );
        writeStream.Flush();

        Stream readStream( STREAM_Read, buffer, MaxPacketSize );
        auto readPacket = make_shared<ConnectionPacket>( PACKET_Connection, channelStructure );
        readPacket->Serialize( readStream );

        simulator.SendPacket( address, readPacket );

        simulator.Update( timeBase );

        auto packet = simulator.ReceivePacket();

        if ( packet )
            connection.ReadPacket( static_pointer_cast<ConnectionPacket>( packet ) );

        assert( connection.GetCounter( Connection::PacketsRead ) <= iteration + 1 );
        assert( connection.GetCounter( Connection::PacketsWritten ) == iteration + 1 );
        assert( connection.GetCounter( Connection::PacketsAcked ) <= iteration + 1 );

        while ( true )
        {
            auto message = messageChannel->ReceiveMessage();

            if ( !message )
                break;

            assert( message->GetId() == numMessagesReceived );
            assert( message->GetType() == MESSAGE_Block );

            auto blockMessage = static_pointer_cast<BlockMessage>( message );

            auto block = blockMessage->GetBlock();

            cout << "received block " << blockMessage->GetId() << " (" << block->size() << " bytes)" << endl;

            assert( block->size() == ( numMessagesReceived + 1 ) * 1024 + numMessagesReceived );
            for ( int i = 0; i < block->size(); ++i )
                assert( (*block)[i] == ( numMessagesReceived + i ) % 256 );

            ++numMessagesReceived;
        }

        if ( numMessagesReceived == NumMessagesSent )
            break;

        connection.Update( timeBase );

        assert( messageChannel->GetCounter( ReliableMessageChannel::MessagesSent ) == NumMessagesSent );
        assert( messageChannel->GetCounter( ReliableMessageChannel::MessagesReceived ) == numMessagesReceived );
        assert( messageChannel->GetCounter( ReliableMessageChannel::MessagesEarly ) == 0 );

        timeBase.time += timeBase.deltaTime;

        iteration++;
    }

    assert( messageChannel->GetCounter( ReliableMessageChannel::MessagesReceived ) == NumMessagesSent );
}

void test_reliable_message_channel_mixture()
{
    cout << "test_reliable_message_channel_mixture" << endl;

    auto channelStructure = make_shared<TestChannelStructure>();
    auto packetFactory = make_shared<PacketFactory>( channelStructure );

    const int MaxPacketSize = 256;

    ConnectionConfig connectionConfig;
    connectionConfig.packetType = PACKET_Connection;
    connectionConfig.maxPacketSize = MaxPacketSize;
    connectionConfig.packetFactory = packetFactory;
    connectionConfig.channelStructure = channelStructure;

    Connection connection( connectionConfig );

    auto messageChannel = static_pointer_cast<ReliableMessageChannel>( connection.GetChannel( 0 ) );

    auto channelConfig = channelStructure->GetConfig(); 

    const int NumMessagesSent = 256;

    for ( int i = 0; i < NumMessagesSent; ++i )
    {
        if ( rand() % 10 )
        {
            auto message = make_shared<TestMessage>();
            message->sequence = i;
            messageChannel->SendMessage( message );
        }
        else
        {
            auto block = make_shared<Block>( ( i + 1 ) * 8 + i, 0 );
            for ( int j = 0; j < block->size(); ++j )
                (*block)[j] = ( i + j ) % 256;
            messageChannel->SendBlock( block );
        }
    }

    TimeBase timeBase;
    timeBase.deltaTime = 0.01f;

    uint64_t numMessagesReceived = 0;

    int iteration = 0;

    Address address( "::1" );

    NetworkSimulator simulator;
    simulator.AddState( { 1.0f, 1.0f, 90 } );

    while ( true )
    {  
        auto writePacket = connection.WritePacket();

        uint8_t buffer[MaxPacketSize];

        Stream writeStream( STREAM_Write, buffer, MaxPacketSize );
        writePacket->Serialize( writeStream );
        writeStream.Flush();

        Stream readStream( STREAM_Read, buffer, MaxPacketSize );
        auto readPacket = make_shared<ConnectionPacket>( PACKET_Connection, channelStructure );
        readPacket->Serialize( readStream );

        simulator.SendPacket( address, readPacket );

        simulator.Update( timeBase );

        auto packet = simulator.ReceivePacket();

        if ( packet )
            connection.ReadPacket( static_pointer_cast<ConnectionPacket>( packet ) );

        assert( connection.GetCounter( Connection::PacketsRead ) <= iteration + 1 );
        assert( connection.GetCounter( Connection::PacketsWritten ) == iteration + 1 );
        assert( connection.GetCounter( Connection::PacketsAcked ) <= iteration + 1 );

        while ( true )
        {
            auto message = messageChannel->ReceiveMessage();

            if ( !message )
                break;

            assert( message->GetId() == numMessagesReceived );

            if ( message->GetType() == MESSAGE_Block )
            {
                assert( message->GetType() == MESSAGE_Block );

                auto blockMessage = static_pointer_cast<BlockMessage>( message );

                auto block = blockMessage->GetBlock();

                cout << "received block " << blockMessage->GetId() << " (" << block->size() << " bytes)" << endl;

                assert( block->size() == ( numMessagesReceived + 1 ) * 8 + numMessagesReceived );
                for ( int i = 0; i < block->size(); ++i )
                    assert( (*block)[i] == ( numMessagesReceived + i ) % 256 );
            }
            else
            {
                assert( message->GetType() == MESSAGE_Test );

                cout << "received message " << message->GetId() << endl;

                auto testMessage = static_pointer_cast<TestMessage>( message );

                assert( testMessage->sequence == numMessagesReceived );
            }

            ++numMessagesReceived;
        }

        if ( numMessagesReceived == NumMessagesSent )
            break;

        connection.Update( timeBase );

        assert( messageChannel->GetCounter( ReliableMessageChannel::MessagesSent ) == NumMessagesSent );
        assert( messageChannel->GetCounter( ReliableMessageChannel::MessagesReceived ) == numMessagesReceived );
        assert( messageChannel->GetCounter( ReliableMessageChannel::MessagesEarly ) == 0 );

        timeBase.time += timeBase.deltaTime;

        iteration++;
    }

    assert( messageChannel->GetCounter( ReliableMessageChannel::MessagesReceived ) == NumMessagesSent );
}

int main()
{
    srand( time( NULL ) );

    try
    {
        test_reliable_message_channel_messages();
        test_reliable_message_channel_small_blocks();
        test_reliable_message_channel_large_blocks();
        test_reliable_message_channel_mixture();
    }
    catch ( runtime_error & e )
    {
        cerr << string( "error: " ) + e.what() << endl;
    }

    return 0;
}
