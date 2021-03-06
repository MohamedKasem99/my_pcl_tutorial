#include <ros/ros.h>
// PCL specific includes
#include <sensor_msgs/PointCloud2.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/console/parse.h>
#include <pcl/sample_consensus/sac_model_plane.h>
#include <pcl/people/ground_based_people_detection_app.h>
#include <pcl/common/time.h>

#include <mutex>
#include <thread>

using namespace std::literals::chrono_literals;

typedef pcl::PointXYZRGBA PointT;
typedef pcl::PointCloud<PointT> PointCloudT;

// PCL viewer //
pcl::visualization::PCLVisualizer viewer("PCL Viewer");

// Mutex: //
std::mutex cloud_mutex;
bool new_cloud_available_flag = false;
enum
{
    COLS = 640,
    ROWS = 480
};

int print_help()
{
    std::cout << "*******************************************************" << std::endl;
    std::cout << "Ground based people detection app options:" << std::endl;
    std::cout << "   --help    <show_this_help>" << std::endl;
    std::cout << "   --svm     <path_to_svm_file>" << std::endl;
    std::cout << "   --conf    <minimum_HOG_confidence (default = -1.5)>" << std::endl;
    std::cout << "   --min_h   <minimum_person_height (default = 1.3)>" << std::endl;
    std::cout << "   --max_h   <maximum_person_height (default = 2.3)>" << std::endl;
    std::cout << "*******************************************************" << std::endl;
    return 0;
}

ros::Publisher pub;
PointCloudT::Ptr cloud(new PointCloudT);
void cloud_cb(const sensor_msgs::PointCloud2::ConstPtr &input)
{

    cloud_mutex.lock(); // for not overwriting the point cloud from another thread
    new_cloud_available_flag = true;
    pcl::PCLPointCloud2 pcl_pc2;
    pcl_conversions::toPCL(*input, pcl_pc2);
    pcl::fromPCLPointCloud2(pcl_pc2, *cloud);
    cloud_mutex.unlock();
    sensor_msgs::PointCloud2 output;
    output = *input;
    // Publish the data.
    pub.publish(output);
}

struct callback_args
{
    // structure used to pass arguments to the callback function
    PointCloudT::Ptr clicked_points_3d;
    pcl::visualization::PCLVisualizer::Ptr viewerPtr;
};

void pp_callback(const pcl::visualization::PointPickingEvent &event, void *args)
{
    struct callback_args *data = (struct callback_args *)args;
    if (event.getPointIndex() == -1)
        return;
    PointT current_point;
    event.getPoint(current_point.x, current_point.y, current_point.z);
    data->clicked_points_3d->points.push_back(current_point);
    // Draw clicked points in red:
    pcl::visualization::PointCloudColorHandlerCustom<PointT> red(data->clicked_points_3d, 255, 0, 0);
    data->viewerPtr->removePointCloud("clicked_points");
    data->viewerPtr->addPointCloud(data->clicked_points_3d, red, "clicked_points");
    data->viewerPtr->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 10, "clicked_points");
    std::cout << current_point.x << " " << current_point.y << " " << current_point.z << std::endl;
}

int main(int argc, char **argv)
{
    // Initialize ROS
    ros::init(argc, argv, "my_pcl_tutorial");
    ros::NodeHandle nh;
    pub = nh.advertise<sensor_msgs::PointCloud2>("/echo_cloud", 1);
    ros::Subscriber sub = nh.subscribe<sensor_msgs::PointCloud2>("/camera/depth/points", 1, cloud_cb);
    if (pcl::console::find_switch(argc, argv, "--help") || pcl::console::find_switch(argc, argv, "-h"))
        return print_help();

    // Algorithm parameters:
    std::string svm_filename = "/home/kasem/catkin_ws/src/my_pcl_tutorial/src/svm.yaml";
    float min_confidence = -1.5;
    float min_height = 1.3;
    float max_height = 2.3;
    float voxel_size = 0.06;
    Eigen::Matrix3f rgb_intrinsics_matrix;
    rgb_intrinsics_matrix << 525, 0.0, 319.5, 0.0, 525, 239.5, 0.0, 0.0, 1.0; // Kinect RGB camera intrinsics

    // Read if some parameters are passed from command line:
    pcl::console::parse_argument(argc, argv, "--svm", svm_filename);
    pcl::console::parse_argument(argc, argv, "--conf", min_confidence);
    pcl::console::parse_argument(argc, argv, "--min_h", min_height);
    pcl::console::parse_argument(argc, argv, "--max_h", max_height);

    // PointCloudT::Ptr cloud(new PointCloudT);
    while (!new_cloud_available_flag && ros::ok())
    {
        ros::spinOnce();
        std::cout << "Nothing was recieved\n";
        std::this_thread::sleep_for(1ms);
    }
    // Create a ROS subscriber for the input point cloud
    cloud_mutex.lock(); // for not overwriting the point cloud

    // Display pointcloud:
    pcl::visualization::PointCloudColorHandlerRGBField<PointT> rgb(cloud);
    viewer.addPointCloud<PointT>(cloud, rgb, "input_cloud");
    viewer.setCameraPosition(0, 0, -2, 0, -1, 0, 0);

    // Add point picking callback to viewer:
    struct callback_args cb_args;
    PointCloudT::Ptr clicked_points_3d(new PointCloudT);
    cb_args.clicked_points_3d = clicked_points_3d;
    cb_args.viewerPtr = pcl::visualization::PCLVisualizer::Ptr(&viewer);
    viewer.registerPointPickingCallback(pp_callback, (void *)&cb_args);
    ROS_INFO("Shift+click on three floor points, then press 'Q'...");
    // Spin until 'Q' is pressed:
    viewer.spin();
    ROS_INFO("done.");

    cloud_mutex.unlock();

    // Ground plane estimation:
    Eigen::VectorXf ground_coeffs;
    ground_coeffs.resize(4);
    std::vector<int> clicked_points_indices;
    for (unsigned int i = 0; i < clicked_points_3d->points.size(); i++)
        clicked_points_indices.push_back(i);
    pcl::SampleConsensusModelPlane<PointT> model_plane(clicked_points_3d);
    model_plane.computeModelCoefficients(clicked_points_indices, ground_coeffs);
    std::cout << "Ground plane: " << ground_coeffs(0) << " " << ground_coeffs(1) << " " << ground_coeffs(2) << " " << ground_coeffs(3) << std::endl;

    // Initialize new viewer:
    pcl::visualization::PCLVisualizer viewer("PCL Viewer"); // viewer initialization
    viewer.setCameraPosition(0, 0, -2, 0, -1, 0, 0);

    // Create classifier for people detection:
    pcl::people::PersonClassifier<pcl::RGB> person_classifier;
    person_classifier.loadSVMFromFile(svm_filename); // load trained SVM

    // People detection app initialization:
    pcl::people::GroundBasedPeopleDetectionApp<PointT> people_detector;       // people detection object
    people_detector.setVoxelSize(voxel_size);                                 // set the voxel size
    people_detector.setIntrinsics(rgb_intrinsics_matrix);                     // set RGB camera intrinsic parameters
    people_detector.setClassifier(person_classifier);                         // set person classifier
    people_detector.setPersonClusterLimits(min_height, max_height, 0.1, 8.0); // set person classifier
                                                                              //  people_detector.setSensorPortraitOrientation(true);             // set sensor orientation to vertical

    // For timing:
    static unsigned count = 0;
    static double last = pcl::getTime();

    // Main loop:
    while (!viewer.wasStopped() && ros::ok())
    {
        if (new_cloud_available_flag && cloud_mutex.try_lock())
        {
            new_cloud_available_flag = false;
            // Perform people detection on the new cloud:
            std::vector<pcl::people::PersonCluster<PointT>> clusters; // vector containing persons clusters
            people_detector.setInputCloud(cloud);
            people_detector.setGround(ground_coeffs);    // set floor coefficients
            people_detector.compute(clusters);           // perform people detection
            ground_coeffs = people_detector.getGround(); // get updated floor coefficients

            // Draw cloud and people bounding boxes in the viewer:
            viewer.removeAllPointClouds();
            viewer.removeAllShapes();
            pcl::visualization::PointCloudColorHandlerRGBField<PointT> rgb(cloud);
            viewer.addPointCloud<PointT>(cloud, rgb, "input_cloud");
            unsigned int k = 0, uncounted;
            for (std::vector<pcl::people::PersonCluster<PointT>>::iterator it = clusters.begin(); it != clusters.end(); ++it)
            {
                std::cout << "Confidence " << uncounted << ": " << it->getPersonConfidence() << "\n";
                if (it->getPersonConfidence() > min_confidence) // draw only people with confidence above a threshold
                {
                    // draw theoretical person bounding box in the PCL viewer:
                    it->drawTBoundingBox(viewer, k);
                    k++;
                }
                uncounted++;
            }
            std::cout << k << " people found" << std::endl;
            viewer.spinOnce();

            // Display average framerate:
            if (++count == 30)
            {
                double now = pcl::getTime();
                std::cout << "Average framerate: " << double(count) / double(now - last) << " Hz" << std::endl;
                count = 0;
                last = now;
            }
            cloud_mutex.unlock();
            // Create a ROS publisher for the output point cloud
            pub = nh.advertise<sensor_msgs::PointCloud2>("output", 1);

            // Spin
            ros::spinOnce();
        }
    }
}