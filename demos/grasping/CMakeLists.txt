#set(source
#  servo_PB_Grasp.cpp
#  servo_PB_Grasp_Tea.cpp
#  servo_PB_grasp_tracking_tea.cpp
#  servo_PB_grasp_tracking_tea_move_head.cpp
#  servo_PB_grasp_tracking_tea_move_head_init_sens.cpp
#  )

#foreach(src ${source})
#  get_filename_component(binary ${src} NAME_WE)
#  qi_create_bin(${binary} ${src})
#  qi_use_lib(${binary} visp_naoqi ALCOMMON ALPROXIES ALVISION)
#endforeach()

#include_directories(${PROJECT_SOURCE_DIR}/demos/grasping)
#qi_create_bin(grasp_tea_mbt grasp_tea_mbt.cpp)
#qi_use_lib(grasp_tea_mbt romeo_tk visp_naoqi ALCOMMON ALPROXIES ALVISION)


#qi_create_bin(grasp_box grasp_box.cpp)
#qi_use_lib(grasp_box romeo_tk visp_naoqi ALCOMMON ALPROXIES ALVISION ALMATH)

qi_create_bin(grasp_box_eye grasp_box_eye.cpp)
qi_create_bin(grasp_box_bh grasp_box_bh.cpp)
qi_create_bin(grasp_box_bh_blob grasp_box_bh_blob.cpp)
#qi_create_bin(grasp_sinatrack_original grasp_sinatrack_original.cpp)
qi_create_bin(grasp_boxes_2arms grasp_boxes_2arms.cpp)
qi_create_bin(grasp_boxes_2arms_move_head grasp_boxes_2arms_move_head.cpp)

qi_use_lib(grasp_box_eye romeo_tk visp_naoqi ALCOMMON ALPROXIES ALVISION ALMATH)
qi_use_lib(grasp_box_bh romeo_tk visp_naoqi ALCOMMON ALPROXIES ALVISION ALMATH)
qi_use_lib(grasp_box_bh_blob romeo_tk visp_naoqi ALCOMMON ALPROXIES ALVISION ALMATH)
#qi_use_lib(grasp_sinatrack_original romeo_tk visp_naoqi ALCOMMON ALPROXIES ALVISION ALMATH)
qi_use_lib(grasp_boxes_2arms romeo_tk visp_naoqi ALCOMMON ALPROXIES ALVISION ALMATH)
qi_use_lib(grasp_boxes_2arms_move_head romeo_tk visp_naoqi ALCOMMON ALPROXIES ALVISION ALMATH)


#subdirs(grasp_box_bh_auto_sinatrack)
