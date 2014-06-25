#include <ros/ros.h>
#include <iostream>
#include <string>
#include <lightweight_vicon_bridge/MocapState.h>
#include <Client.h>

int main(int argc, char** argv)
{
    ros::init(argc, argv, "lightweight_vicon_bridge");
    ROS_INFO("Starting lightweight vicon bridge...");
    ros::NodeHandle nh;
    ros::NodeHandle nhp("~");
    ROS_INFO("Loading parameters...");
    std::string tracker_hostname;
    std::string tracker_port;
    std::string tracker_frame_name;
    std::string tracker_name;
    std::string tracker_topic;
    std::string stream_mode;
    nhp.param(std::string("tracker_hostname"), tracker_hostname, std::string("192.168.2.161"));
    nhp.param(std::string("tracker_port"), tracker_port, std::string("801"));
    nhp.param(std::string("tracker_frame_name"), tracker_frame_name, std::string("mocap_world"));
    nhp.param(std::string("tracker_name"), tracker_name, std::string("vicon"));
    nhp.param(std::string("tracker_topic"), tracker_topic, std::string("mocap_tracking"));
    nhp.param(std::string("stream_mode"), stream_mode, std::string("ServerPush"));
    // Check the stream mode
    if (stream_mode == std::string("ServerPush") || stream_mode == std::string("ClientPullPreFetch") || stream_mode == std::string("ClientPull"))
    {
        ROS_INFO("Starting in %s mode", stream_mode.c_str());
    }
    else
    {
        ROS_FATAL("Invalid stream mode %s, valid options are ServerPush, ClientPullPreFetch, and ClientPull", stream_mode.c_str());
        exit(-1);
    }
    // Assemble the full hostname
    tracker_hostname = tracker_hostname + ":" + tracker_port;
    ROS_INFO("Connecting to Vicon Tracker (DataStream SDK) at hostname %s", tracker_hostname.c_str());
    // Make the ROS publisher
    ros::Publisher mocap_pub = nh.advertise<lightweight_vicon_bridge::MocapState>(tracker_topic, 1, false);
    // Initialize the DataStream SDK
    ViconDataStreamSDK::CPP::Client sdk_client;
    ROS_INFO("Connecting to server...");
    sdk_client.Connect(tracker_hostname);
    usleep(10000);
    while (!sdk_client.IsConnected().Connected && ros::ok())
    {
        ROS_WARN("...taking a while to connect, trying again...");
        sdk_client.Connect(tracker_hostname);
        usleep(10000);
    }
    ROS_INFO("...connected!");
    // Enable data
    sdk_client.EnableSegmentData();
    // Set the axes (right-handed, X-forwards, Y-right, Z-up, same as ROS)
    sdk_client.SetAxisMapping(ViconDataStreamSDK::CPP::Direction::Forward, ViconDataStreamSDK::CPP::Direction::Left, ViconDataStreamSDK::CPP::Direction::Up);
    // Set streaming mode
    if (stream_mode == "ServerPush")
    {
        sdk_client.SetStreamMode(ViconDataStreamSDK::CPP::StreamMode::ServerPush);
    }
    else if (stream_mode == "ClientPullPreFetch")
    {
        sdk_client.SetStreamMode(ViconDataStreamSDK::CPP::StreamMode::ClientPullPreFetch);
    }
    else if (stream_mode == "ClientPull")
    {
        sdk_client.SetStreamMode(ViconDataStreamSDK::CPP::StreamMode::ClientPull);
    }
    else
    {
        ROS_FATAL("Invalid stream mode");
        exit(-2);
    }
    // Start streaming data
    ROS_INFO("Streaming data...");
    while (ros::ok())
    {
        // Get a new frame and process it
        if (sdk_client.GetFrame().Result == ViconDataStreamSDK::CPP::Result::Success)
        {
            double total_latency = sdk_client.GetLatencyTotal().Total;
            ros::Duration latency_duration(total_latency);
            ros::Time current_time = ros::Time::now();
            ros::Time frame_time = current_time - latency_duration;
            lightweight_vicon_bridge::MocapState state_msg;
            state_msg.header.frame_id = tracker_frame_name;
            state_msg.header.stamp = frame_time;
            state_msg.tracker_name = tracker_name;
            // Loop through the tracked objects and add them to the message
            unsigned int objects = sdk_client.GetSubjectCount().SubjectCount;
            for (unsigned int idx_o = 0; idx_o < objects; idx_o++)
            {
                std::string object_name = sdk_client.GetSubjectName(idx_o).SubjectName;
                lightweight_vicon_bridge::MocapObject object_msg;
                object_msg.name = object_name;
                // Loop through the segments of the object
                unsigned int segments = sdk_client.GetSegmentCount(object_name).SegmentCount;
                for (unsigned int idx_s = 0; idx_s < segments; idx_s++)
                {
                    std::string segment_name = sdk_client.GetSegmentName(object_name, idx_s).SegmentName;
                    ViconDataStreamSDK::CPP::Output_GetSegmentGlobalTranslation segment_position = sdk_client.GetSegmentGlobalTranslation(object_name, segment_name);
                    ViconDataStreamSDK::CPP::Output_GetSegmentGlobalRotationQuaternion segment_rotation = sdk_client.GetSegmentGlobalRotationQuaternion(object_name, segment_name);
                    lightweight_vicon_bridge::MocapSegment segment_msg;
                    segment_msg.name = segment_name;
                    segment_msg.transform.translation.x = segment_position.Translation[0];
                    segment_msg.transform.translation.y = segment_position.Translation[1];
                    segment_msg.transform.translation.z = segment_position.Translation[2];
                    segment_msg.transform.rotation.x = segment_rotation.Rotation[0];
                    segment_msg.transform.rotation.y = segment_rotation.Rotation[1];
                    segment_msg.transform.rotation.z = segment_rotation.Rotation[2];
                    segment_msg.transform.rotation.w = segment_rotation.Rotation[3];
                    // Determine if the object is occluded or not
                    if (segment_position.Occluded || segment_rotation.Occluded)
                    {
                        segment_msg.occluded = true;
                    }
                    else
                    {
                        segment_msg.occluded = false;
                    }
                    // Store the segment
                    object_msg.segments.push_back(segment_msg);
                }
                // Store the object
                state_msg.tracked_objects.push_back(object_msg);
            }
            // Finish the message and publish
            mocap_pub.publish(state_msg);
        }
        // Handle ROS stuff
        ros::spinOnce();
    }
    sdk_client.DisableSegmentData();
    sdk_client.Disconnect();
    return 0;
}