﻿/**
 *
 * Using the derivative of the Jacobian we want to maximaze the manipulability.
 *
 */

#include <iostream>
#include <string>
#include <list>
#include <iterator>

// Aldebaran includes.
#include <alproxies/altexttospeechproxy.h>

// ViSP includes.
#include <visp/vpDisplayX.h>
#include <visp/vpImage.h>
#include <visp/vpImageConvert.h>
#include <visp/vpDot2.h>
#include <visp/vpImageIo.h>
#include <visp/vpMbEdgeKltTracker.h>
#include <visp/vpMeterPixelConversion.h>
#include <visp/vpFeatureBuilder.h>
#include <visp/vpXmlParserCamera.h>
#include <visp/vpXmlParserHomogeneousMatrix.h>
#include <visp/vpPlot.h>
#include <visp/vpPoint.h>


#include <visp_naoqi/vpNaoqiGrabber.h>
#include <visp_naoqi/vpNaoqiRobot.h>
#include <visp_naoqi/vpNaoqiConfig.h>

#include <vpQRCodeTracker.h>
#include <vpServoArm.h>
#include <vpRomeoTkConfig.h>
#include <vpJointLimitAvoidance.h>

using namespace AL;


vpColVector computeSecondaryTaskManipulability(vpMatrix P, vpMatrix J, std::vector <vpMatrix> dJ)
{
    const unsigned int n = dJ.size();

    vpColVector z(n);

    double alpha = 0.9;

    for (unsigned int i = 0; i < n; i++)
    {
        double detJJt = (J * J.transpose()).det();

        vpMatrix dJJinv = dJ[i] * J.pseudoInverse();

        double trace = 0.0;
        for (unsigned int k = 0; k < dJJinv.getCols(); k++)
            trace += dJJinv[k][k];

        z[i] = alpha * sqrt(detJJt) * trace;

    }


    return P * z;

}


void printPose(const std::string &text, const vpHomogeneousMatrix &cMo)
{
    vpTranslationVector t;
    cMo.extract(t);
    vpRotationMatrix R;
    cMo.extract(R);
    vpThetaUVector tu(R);

    std::cout << text;
    for (unsigned int i=0; i < 3; i++)
        std::cout << t[i] << " ";
    for (unsigned int i=0; i < 3; i++)
        std::cout << vpMath::deg(tu[i]) << " ";
    std::cout << std::endl;
}

int main(int argc, const char* argv[])
{

    std::string opt_ip = "198.18.0.1";;
    bool opt_plotter_arm = false;
    bool opt_plotter_qrcode_pose = false;
    bool opt_learn = false;
    bool opt_plotter_q_sec_arm = false;
    bool opt_plotter_q = false;
    bool opt_right_arm = false;
    bool opt_plotter_cond = false;


    for (unsigned int i=0; i<argc; i++) {
        if (std::string(argv[i]) == "--ip")
            opt_ip = argv[i+1];
        else if (std::string(argv[i]) == "--learn")
            opt_learn = true;
        else if (std::string(argv[i]) == "--rarm")
            opt_right_arm = true;
        else if (std::string(argv[i]) == "--plot-arm")
            opt_plotter_arm = true;
        else if (std::string(argv[i]) == "--plot-qrcode-pose")
            opt_plotter_qrcode_pose = true;
        else if (std::string(argv[i]) == "--plot-q-sec-arm")
            opt_plotter_q_sec_arm = true;
        else if (std::string(argv[i]) == "--plot-cond")
            opt_plotter_cond = true;
        else if (std::string(argv[i]) == "--plot-q")
            opt_plotter_q = true;
        else if (std::string(argv[i]) == "--help") {
            std::cout << "Usage: " << argv[0] << "[--ip <robot address>] [--learn][--plot-cond][--rarm] [--plot-arm] [--plot-qrcode-pose] [--plot-q-sec-arm] [--plot-q] [--help]" << std::endl;
            return 0;
        }
    }

    std::string suffix;
    std::string chain_name;
    if (opt_right_arm)
    {
        suffix = "_r";
        chain_name = "RArm";
    }
    else
    {
        suffix = "_l";
        chain_name = "LArm";
    }

    std::string learned_filename = "learned_cdMo.xml";
    std::string learned_transform_name = "cdMo" + suffix;

    // Check if the desired position was learned
    if (!opt_learn) {
        if (! vpIoTools::checkFilename(learned_filename)) {
            std::cout << "\nError: You should first learn the desired position using [--learn] option." << std::endl;
            std::cout << "\nRun: \"" << argv[0] << " --help\" to get all the options.\n" <<  std::endl;
            return 0;
        }
    }


    /** Open the grabber for the acquisition of the images from the robot*/
    vpNaoqiGrabber g;
    g.setFramerate(15);
    g.setCamera(0);
    if (! opt_ip.empty())
        g.setRobotIp(opt_ip);
    g.open();

    vpCameraParameters cam = g.getCameraParameters(vpCameraParameters::perspectiveProjWithoutDistortion);
    std::cout << "Camera parameters: " << cam << std::endl;

    /** Create a new istance NaoqiRobot*/
    vpNaoqiRobot robot;
    if (! opt_ip.empty())
        g.setRobotIp(opt_ip);
    robot.open();

    /** Initialization Visp Image, display and camera paramenters*/
    vpImage<unsigned char> I(g.getHeight(), g.getWidth());
    vpDisplayX d(I);
    vpDisplay::setTitle(I, "Right camera view");


    // Initialize the qrcode tracker
    bool status_qrcode_tracker;
    vpHomogeneousMatrix cMo_qrcode;
    vpQRCodeTracker qrcode_tracker;
    qrcode_tracker.setCameraParameters(cam);
    if (opt_right_arm)
    {
        qrcode_tracker.setQRCodeSize(0.045);
        qrcode_tracker.setMessage("romeo_right_arm");
    }
    else
    {
        qrcode_tracker.setQRCodeSize(0.045);
        qrcode_tracker.setMessage("romeo_left_arm");
    }

    // Constant transformation Target Frame to Arm end-effector (WristPitch)
    vpHomogeneousMatrix oMe_Arm;

    std::string filename_transform = std::string(ROMEOTK_DATA_FOLDER) + "/transformation.xml";
    std::string name_transform = "qrcode_M_e_" + chain_name;
    vpXmlParserHomogeneousMatrix pm; // Create a XML parser

    if (pm.parse(oMe_Arm, filename_transform, name_transform) != vpXmlParserHomogeneousMatrix::SEQUENCE_OK) {
        std::cout << "Cannot found the homogeneous matrix named " << name_transform << "." << std::endl;
        return 0;
    }
    else
        std::cout << "Homogeneous matrix " << name_transform <<": " << std::endl << oMe_Arm << std::endl;

    // Create twist matrix from target Frame to Arm end-effector (WristPitch)
    vpVelocityTwistMatrix oVe_LArm(oMe_Arm);

    bool grasp_servo_converged = false;
    vpServoArm servo_arm; // Initialize arm servoing

    std::vector<std::string> jointNames_arm =  robot.getBodyNames(chain_name);
    jointNames_arm.pop_back(); // Delete last joints LHand, that we don't consider in the servo


    int numJoints = jointNames_arm.size();

    // Initialize the joint avoidance scheme from the joint limits
    vpColVector jointMin = robot.getJointMin(chain_name);
    vpColVector jointMax = robot.getJointMax(chain_name);
    // Vector secondary task
    vpColVector q2 (numJoints);




    // Vector of joint positions
    vpColVector q;

    //Vector data for plotting
    vpColVector data(13);
    vpColVector Qmiddle(numJoints);

    // Joints limits and paramenters
    std::cout << "Joint limits arm: " << std::endl;
    for (unsigned int i=0; i< numJoints; i++)
    {
        Qmiddle[i] = ( jointMin[i] + jointMax[i]) /2.;
        std::cout << " Joint " << i << " " << jointNames_arm[i]
                     << ": min=" << vpMath::deg(jointMin[i])
                     << " max=" << vpMath::deg(jointMax[i]) << std::endl;
    }
    double ro = 0.2;
    double ro1 = 0.4;


    // Initialize arm open loop servoing
    double servo_time_init = 0;

    //Condition number Jacobian Arm
    double cond = 0.0;

    //Set the stiffness
    robot.setStiffness(jointNames_arm, 1.f);
    vpColVector q_dot_larm(numJoints, 0);

    vpColVector q_l0_min(numJoints);
    vpColVector q_l0_max(numJoints);
    vpColVector q_l1_min(numJoints);
    vpColVector q_l1_max(numJoints);

    robot.getProxy()->setCollisionProtectionEnabled(chain_name,false);

    // Common
    vpMouseButton::vpMouseButtonType button;
    unsigned long loop_iter = 0;

    bool onlyTranslation = false;

    // Plotter


    vpPlot *plotter_cond;
    if (opt_plotter_cond) {
        plotter_cond = new vpPlot(1, I.getHeight(), I.getWidth()*2, I.display->getWindowXPosition()+I.getWidth()+90, I.display->getWindowYPosition()+10, "Loop time");
        plotter_cond->initGraph(0, 1);
    }


    vpPlot *plotter_arm;
    if (opt_plotter_arm) {
        plotter_arm = new vpPlot(2, I.getHeight()*2, I.getWidth()*2, I.display->getWindowXPosition()+I.getWidth()+90, I.display->getWindowYPosition(), "Visual servoing");
        if (onlyTranslation)
            plotter_arm->initGraph(0, 3); // visual features
        else
            plotter_arm->initGraph(0, 6); // visual features
        plotter_arm->initGraph(1, q_dot_larm.size()); // d_dot
        plotter_arm->setTitle(0, "Visual features error");
        plotter_arm->setTitle(1, "joint velocities");
        plotter_arm->setLegend(0, 0, "tx");
        plotter_arm->setLegend(0, 1, "ty");
        plotter_arm->setLegend(0, 2, "tz");
        if (!onlyTranslation)
        {
            plotter_arm->setLegend(0, 3, "tux");
            plotter_arm->setLegend(0, 4, "tuy");
            plotter_arm->setLegend(0, 5, "tuz");
        }
    }
    vpPlot *plotter_qrcode_pose;
    if (opt_plotter_qrcode_pose) {
        plotter_qrcode_pose = new vpPlot(2, I.getHeight()*2, I.getWidth()*2, I.display->getWindowXPosition()+ 3* I.getWidth(), I.display->getWindowYPosition(), "Qrcode pose");
        plotter_qrcode_pose->initGraph(0, 3); // translations
        plotter_qrcode_pose->initGraph(1, 3); // rotations
        plotter_qrcode_pose->setTitle(0, "Pose translation");
        plotter_qrcode_pose->setTitle(1, "Pose theta u");
        plotter_qrcode_pose->setLegend(0, 0, "tx");
        plotter_qrcode_pose->setLegend(0, 1, "ty");
        plotter_qrcode_pose->setLegend(0, 2, "tz");
        plotter_qrcode_pose->setLegend(1, 0, "tux");
        plotter_qrcode_pose->setLegend(1, 1, "tuy");
        plotter_qrcode_pose->setLegend(1, 2, "tuz");
    }

    vpPlot *plotter_q_sec_arm;
    if (opt_plotter_q_sec_arm) {
        plotter_q_sec_arm = new vpPlot(2, I.getHeight()*2, I.getWidth()*2, I.display->getWindowXPosition()+ I.getWidth()+50, I.display->getWindowYPosition()+ 2*I.getHeight() + 60, "Secondary Task");
        plotter_q_sec_arm->initGraph(0, 7); // translations
        plotter_q_sec_arm->initGraph(1, 7); // rotations

        plotter_q_sec_arm->setTitle(0, "DQ2 values");
        plotter_q_sec_arm->setTitle(1, "DQ tot values");

        plotter_q_sec_arm->setLegend(0, 0, "LshouderPitch");
        plotter_q_sec_arm->setLegend(0, 1, "LShoulderYaw");
        plotter_q_sec_arm->setLegend(0, 2, "LElbowRoll");
        plotter_q_sec_arm->setLegend(0, 3, "LElbowYaw");
        plotter_q_sec_arm->setLegend(0, 4, "LWristRoll");
        plotter_q_sec_arm->setLegend(0, 5, "LWristYaw");
        plotter_q_sec_arm->setLegend(0, 6, "LWristPitch");

        plotter_q_sec_arm->setLegend(1, 0, "LshouderPitch");
        plotter_q_sec_arm->setLegend(1, 1, "LShoulderYaw");
        plotter_q_sec_arm->setLegend(1, 2, "LElbowRoll");
        plotter_q_sec_arm->setLegend(1, 3, "LElbowYaw");
        plotter_q_sec_arm->setLegend(1, 4, "LWristRoll");
        plotter_q_sec_arm->setLegend(1, 5, "LWristYaw");
        plotter_q_sec_arm->setLegend(1, 6, "LWristPitch");

    }


    vpPlot *plotter_q;
    if (opt_plotter_q) {

        plotter_q = new vpPlot(1, I.getHeight()*2, I.getWidth()*2, I.display->getWindowXPosition()+3*I.getWidth(), I.display->getWindowYPosition()+ 2*I.getHeight() + 60, "Values of q and limits");
        plotter_q->initGraph(0, 13);

        plotter_q->setTitle(0, "Q1 values");

        plotter_q->setLegend(0, 0, "LshouderPitch");
        plotter_q->setLegend(0, 1, "LShoulderYaw");
        plotter_q->setLegend(0, 2, "LElbowRoll");
        plotter_q->setLegend(0, 3, "LElbowYaw");
        plotter_q->setLegend(0, 4, "LWristRoll");
        plotter_q->setLegend(0, 5, "LWristYaw");
        plotter_q->setLegend(0, 6, "LWristPitch");

        plotter_q->setLegend(0, 7, "Low Limits");
        plotter_q->setLegend(0, 8, "Upper Limits");
        plotter_q->setLegend(0, 9, "l0 min");
        plotter_q->setLegend(0, 10, "l0 max");
        plotter_q->setLegend(0, 11, "l1 min");
        plotter_q->setLegend(0, 12, "l1 max");

        plotter_q->setColor(0, 7,vpColor::darkRed);
        plotter_q->setColor(0, 9,vpColor::darkRed);
        plotter_q->setColor(0, 11,vpColor::darkRed);


        plotter_q->setColor(0, 8,vpColor::darkRed);
        plotter_q->setColor(0, 10,vpColor::darkRed);
        plotter_q->setColor(0, 12,vpColor::darkRed);
        plotter_q->setThickness(0, 7,2);

    }




    vpHomogeneousMatrix cdMo_learned;

    if (! opt_learn) {
        vpXmlParserHomogeneousMatrix pm;
        if (pm.parse(cdMo_learned, learned_filename, learned_transform_name) != vpXmlParserHomogeneousMatrix::SEQUENCE_OK) {
            std::cout << "Cannot found the homogeneous matrix named " << learned_transform_name<< "." << std::endl;
            return false;
        }
        else
            std::cout << "Homogeneous matrix " << learned_transform_name <<": " << std::endl << cdMo_learned << std::endl;
    }


    // Move the head in the default position

    AL::ALValue names_head  = AL::ALValue::array("HeadPitch","HeadRoll", "NeckPitch", "NeckYaw");
    AL::ALValue angles_head ;
    if(opt_right_arm)
        angles_head      = AL::ALValue::array(vpMath::rad(15), vpMath::rad(0), vpMath::rad(10), vpMath::rad(-5));
    else
        angles_head      = AL::ALValue::array(vpMath::rad(15), vpMath::rad(0), vpMath::rad(10), vpMath::rad(6));

    float fractionMaxSpeed  = 0.2f;
    robot.getProxy()->setStiffnesses(names_head, AL::ALValue::array(1.0f, 1.0f, 1.0f, 1.0f));
    qi::os::sleep(1.0f);
    robot.getProxy()->setAngles(names_head, angles_head, fractionMaxSpeed);
    qi::os::sleep(2.0f);


    while(1) {
        double loop_time_start = vpTime::measureTimeMs();
        g.acquire(I);
        vpDisplay::display(I);

        vpDisplay::displayText(I, vpImagePoint(I.getHeight() - 10, 10), "Right click to quit", vpColor::red);

        bool click_done = vpDisplay::getClick(I, button, false);

        // track qrcode
        status_qrcode_tracker = qrcode_tracker.track(I);


        if (status_qrcode_tracker ) { // display the tracking results
            cMo_qrcode = qrcode_tracker.get_cMo();
            printPose("cMo qrcode: ", cMo_qrcode);
            // The qrcode frame is only displayed when PBVS is active or learning
            vpDisplay::displayFrame(I, cMo_qrcode, cam, 0.04, vpColor::none, 3);
            vpDisplay::displayPolygon(I, qrcode_tracker.getCorners(), vpColor::green, 2);
        }

        // learn the desired position
        if (status_qrcode_tracker && opt_learn) {
            vpDisplay::displayText(I, 10, 10, "Left click to learn desired position", vpColor::red);
            if (click_done && button == vpMouseButton::button1) {
                vpXmlParserHomogeneousMatrix p; // Create a XML parser
                cdMo_learned = qrcode_tracker.get_cMo();

                if (p.save(cdMo_learned, learned_filename, learned_transform_name) != vpXmlParserHomogeneousMatrix::SEQUENCE_OK)
                {
                    std::cout << "Cannot save the homogeneous matrix cdMo" << std::endl;
                    return false;
                }
                printPose("Learned pose: ", cdMo_learned);
                return 0;
            }
        }

        // Visual servo of the head centering teabox and qrcode
        if (status_qrcode_tracker && !opt_learn ) {
            static bool first_time = true;
            if (first_time) {
                std::cout << "-- Start visual servoing of the arm" << std::endl;
                servo_time_init = vpTime::measureTimeSecond();
                first_time = false;
            }

            // Servo arm
            if (! grasp_servo_converged) {
                //servo_arm.setLambda(0.2);
                vpAdaptiveGain lambda(0.8, 0.06, 8);

                servo_arm.setLambda(lambda);

                vpMatrix tJe;
                vpMatrix eJe = robot.get_eJe(chain_name, tJe);

                servo_arm.set_eJe(eJe);
                servo_arm.m_task.set_cVe(oVe_LArm);


                vpHomogeneousMatrix cdMc = (cdMo_learned.inverse() * cMo_qrcode) ;
                printPose("cdMc: ", cdMc);
                servo_arm.setCurrentFeature(cdMc) ;

                vpDisplay::displayFrame(I, cdMo_learned , cam, 0.025, vpColor::none, 2);

                q_dot_larm = - servo_arm.computeControlLaw(servo_time_init);

                std::cout << "Vel arm: " << q_dot_larm.t() << std::endl;

                vpColVector e = servo_arm.getError();
                vpMatrix TaskJac = servo_arm.getTaskJacobian();
                vpMatrix TaskJacPseudoInv = servo_arm.getTaskJacobianPseudoInverse();
                vpMatrix L = servo_arm.m_task.getInteractionMatrix();

                //        vpMatrix Ieye;
                //        Ieye.eye(q_dot_larm.size());
                //        std::cout <<"OperatorVisp " << std::endl << Ieye-  servo_arm.m_task.getWpW()  << std::endl;

                //std::cout << "cVo: " << std::endl << cVtorso*torsoVo << std::endl;
                // std::cout << "L: " << std::endl << L << std::endl;
                //std::cout << "oJo: " << std::endl << oJo << std::endl;

                q = robot.getPosition(jointNames_arm);

                q2 = computeQdotLimitAvoidance(e, TaskJac, TaskJacPseudoInv, jointMin, jointMax, q, q_dot_larm,ro,ro1, q_l0_min, q_l0_max, q_l1_min, q_l1_max);


                // Max manipulability

                vpMatrix P(numJoints,numJoints);
                P = computeP(e,TaskJac,TaskJacPseudoInv,numJoints);

                std::vector <vpMatrix> dJ = robot.get_d_eJe(chain_name);

                vpColVector q_man = computeSecondaryTaskManipulability(P,tJe,dJ);


                vpMatrix v;
                vpColVector w;

                tJe.svd(w, v);
                std::cout << "singular values:/n" << w << std::endl;
                cond = w[0]/w[5];


                //cond = TaskJacPseudoInv.cond();

                std::cout << "Cond " << cond <<std::endl;

                std::cout << "tJe " << tJe <<std::endl;


                std::cout << "q_man: " << std::endl << q_man << std::endl;
                //robot.setVelocity(jointNames_arm, q_dot_larm+q2);
                robot.setVelocity(jointNames_arm, q_dot_larm+ q_man);

                if(opt_plotter_cond)
                    plotter_cond->plot(0, 0, loop_iter, cond);

            }
        }
        else if(! status_qrcode_tracker) {
            robot.stop(jointNames_arm);
        }

        if (click_done && button == vpMouseButton::button3) { // Quit the loop
            robot.stop(jointNames_arm);
            break;
        }

        double loop_time = vpTime::measureTimeMs() - loop_time_start;

        if (opt_plotter_arm && ! opt_learn) {
            plotter_arm->plot(0, loop_iter, servo_arm.m_task.getError());
            plotter_arm->plot(1, loop_iter, q_dot_larm);
        }


        if (opt_plotter_qrcode_pose && status_qrcode_tracker) {
            vpPoseVector p(cMo_qrcode);
            vpColVector cto(3);
            vpColVector cthetauo(3);
            for(size_t i=0; i<3; i++) {
                cto[i] = p[i];
                cthetauo[i] = vpMath::deg(p[i+3]);
            }

            plotter_qrcode_pose->plot(0, loop_iter, cto);
            plotter_qrcode_pose->plot(1, loop_iter, cthetauo);
        }

        if (opt_plotter_q_sec_arm  && status_qrcode_tracker)
        {
            plotter_q_sec_arm->plot(0,loop_iter,q2);
            plotter_q_sec_arm->plot(1,loop_iter,q_dot_larm + q2);

        }

        if (opt_plotter_q  && status_qrcode_tracker)
        {

            // q normalized between (entre -1 et 1)
            for (unsigned int i=0 ; i < numJoints ; i++) {
                data[i] = (q[i] - Qmiddle[i]) ;
                data[i] /= (jointMax[i] - jointMin[i]) ;
                data[i]*=2 ;
            }



            data[numJoints] = -1.0;
            data[numJoints+1] = 1.0;

            unsigned int joint = 1;
            double tQmin_l0 = jointMin[joint] + ro *(jointMax[joint] - jointMin[joint]);
            double tQmax_l0 = jointMax[joint] - ro *(jointMax[joint] - jointMin[joint]);

            double tQmin_l1 =  tQmin_l0 - ro * ro1 * (jointMax[joint] - jointMin[joint]);
            double tQmax_l1 =  tQmax_l0 + ro * ro1 * (jointMax[joint] - jointMin[joint]);

            data[numJoints+2] = 2*(tQmin_l0 - Qmiddle[joint])/(jointMax[joint] - jointMin[joint]);
            data[numJoints+3] = 2*(tQmax_l0 - Qmiddle[joint])/(jointMax[joint] - jointMin[joint]);

            data[numJoints+4] =  2*(tQmin_l1 - Qmiddle[joint])/(jointMax[joint] - jointMin[joint]);
            data[numJoints+5] =  2*(tQmax_l1 - Qmiddle[joint])/(jointMax[joint] - jointMin[joint]);

            plotter_q->plot(0,0,loop_iter,data[0]);
            plotter_q->plot(0,1,loop_iter,data[1]);
            plotter_q->plot(0,2,loop_iter,data[2]);
            plotter_q->plot(0,3,loop_iter,data[3]);
            plotter_q->plot(0,4,loop_iter,data[4]);
            plotter_q->plot(0,5,loop_iter,data[5]);
            plotter_q->plot(0,6,loop_iter,data[6]);

            plotter_q->plot(0,7,loop_iter,data[7]);
            plotter_q->plot(0,8,loop_iter,data[8]);

            plotter_q->plot(0,9,loop_iter,data[9]);
            plotter_q->plot(0,10,loop_iter,data[10]);
            plotter_q->plot(0,11,loop_iter,data[11]);
            plotter_q->plot(0,12,loop_iter,data[12]);


        }


        vpDisplay::flush(I) ;
        //std::cout << "Loop time: " << vpTime::measureTimeMs() - loop_time_start << std::endl;

        loop_iter ++;
    }


    while(1)
    {

        bool click_done = vpDisplay::getClick(I, button, false);

        if (click_done && button == vpMouseButton::button3) { // Quit the loop
            break;
        }

    }


    if (opt_plotter_arm)
        delete plotter_arm;
    if (opt_plotter_qrcode_pose)
        delete plotter_qrcode_pose;
    if (opt_plotter_q)
        delete plotter_q;
    if (opt_plotter_q_sec_arm)
        delete plotter_q_sec_arm;
    if (opt_plotter_cond)
        delete plotter_cond;


    return 0;
}

