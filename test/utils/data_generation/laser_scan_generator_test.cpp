#include <gtest/gtest.h>

#include <sstream>

#include "../../../src/core/math_utils.h"
#include "../../../src/core/robot_pose.h"
#include "../../../src/core/maps/plain_grid_map.h"
#include "../../../src/utils/data_generation/grid_map_patcher.h"
#include "../../../src/utils/data_generation/laser_scan_generator.h"

const std::string Cecum_Corridor_Map_Patch =
  "+-+\n"
  "| |\n"
  "| |";
constexpr int Cecum_Patch_W = 3, Cecum_Patch_H = 3;
constexpr int Cecum_Free_W = 1, Cecum_Free_H = 2;
// wrt top-left
constexpr int Cecum_Free_X_Start = 1, Cecum_Free_Y_Start = -1;

class TestGridCell : public GridCell {
public:
  TestGridCell() : GridCell{Occupancy{0, 0}} {}
  std::unique_ptr<GridCell> clone() const override {
    return std::make_unique<TestGridCell>(*this);
  }
};

class LaserScanGeneratorTest : public ::testing::Test {
protected: // methods
  LaserScanGeneratorTest()
    : map{std::make_shared<TestGridCell>(),
          GridMapParams{Map_Width, Map_Height, Map_Scale}}
    , rpose{map.scale() / 2, map.scale() / 2, 0} /* middle of a cell */ {}
protected:
  using ScanPoints = std::vector<ScanPoint>;
protected: // consts
  static constexpr int Map_Width = 100;
  static constexpr int Map_Height = 100;
  static constexpr double Map_Scale = 1;

  static constexpr int Patch_Scale = 10;
protected: // fields
  void prepare_map_and_robot_pose(const RobotPoseDelta &rpd,
                                  int scale = Patch_Scale) {
    patch_map_with_cecum(scale);
    rpose += rpd;
  }

  void patch_map_with_cecum(int scale) {
    auto gm_patcher = GridMapPatcher{};
    std::stringstream raster{Cecum_Corridor_Map_Patch};
    gm_patcher.apply_text_raster(map, raster, {}, scale, scale);
  }

  void check_scan_points(const ScanPoints &expected, const ScanPoints &actual,
                         const ScanPoint &sp_err = {Map_Scale / 2, 0.001}) {
    ASSERT_EQ(expected.size(), actual.size());
    for (std::size_t sp_i = 0; sp_i < expected.size(); ++sp_i) {
      check_scan_point(expected[sp_i], actual[sp_i], sp_err);
      auto expected_occ = actual[sp_i].is_occupied ? 1.0 : 0;
      auto sp_coord = map.world_to_cell_by_vec(rpose.x, rpose.y,
        actual[sp_i].range, rpose.theta + actual[sp_i].angle);
      ASSERT_NEAR(expected_occ, map[sp_coord], 0.01);
    }
  }

  void check_scan_point(const ScanPoint &expected, const ScanPoint &actual,
                        const ScanPoint &abs_err) {
    ASSERT_NEAR(expected.angle, actual.angle, abs_err.angle);
    ASSERT_NEAR(expected.range, actual.range,
                // scale absolute range error according to relative angle
                std::abs(abs_err.range / std::cos(rpose.theta + actual.angle)));
  }

  // TODO: move to debug utils
  void dbg_print_map_pose(int scale = Patch_Scale) {
    auto rc = map.world_to_cell(rpose.x, rpose.y);
    std::cout << "(" << rpose.x << " " << rpose.y << ") -> " << rc << std::endl;
    auto c = DiscretePoint2D{0, 0};
    for (c.y = 0; -Cecum_Patch_H * scale < c.y; --c.y) {
      for (c.x = 0; c.x < Cecum_Patch_W * scale; ++c.x) {
        std::cout << (rc == c ? "*" : std::to_string(map[c] == 1.0 ? 1 : 0));
      }
      std::cout << std::endl;
    }
  }

protected: // fields
  UnboundedPlainGridMap map;
  RobotPose rpose;
  LaserScanGenerator lsg{LaserScannerParams{150, deg2rad(90), deg2rad(180)}};
};

//------------------------------------------------------------------------------
// Degenerate cases

TEST_F(LaserScanGeneratorTest, emptyMap4beams) {
  auto scan = lsg.generate_2D_laser_scan(map, rpose, 1);

  const auto Expected_SPs = ScanPoints{};
  check_scan_points(Expected_SPs, scan.points);
}

TEST_F(LaserScanGeneratorTest, insideObstacle4beams) {
  auto occ_obs = AreaOccupancyObservation{true, Occupancy{1.0, 1.0},
                                          Point2D{rpose.x, rpose.y}, 1};
  map[map.world_to_cell(rpose.x, rpose.y)] += occ_obs;

  auto scan = lsg.generate_2D_laser_scan(map, rpose, 1);
  const auto Expected_SPs = ScanPoints {{0, deg2rad(-180)}, {0, deg2rad(-90)},
                                        {0, deg2rad(0)}, {0, deg2rad(90)}};

  check_scan_points(Expected_SPs, scan.points);
}

//------------------------------------------------------------------------------
// Perpendicular wall facing

TEST_F(LaserScanGeneratorTest, leftOfCecumEntranceFacingRight4beams) {
  auto pose_delta = RobotPoseDelta{
    (Cecum_Free_X_Start * Patch_Scale) * map.scale(),
    (-Cecum_Patch_H * Patch_Scale + 1) * map.scale(),
    deg2rad(0)
  };
  prepare_map_and_robot_pose(pose_delta, Patch_Scale);
  auto scan = lsg.generate_2D_laser_scan(map, rpose, 1);

  const double scale = map.scale() * Patch_Scale;
  const auto Expected_SPs = ScanPoints{{map.scale(), deg2rad(-180)},
                                       {Cecum_Free_W * scale, deg2rad(0)},
                                       {Cecum_Free_H * scale, deg2rad(90)}};
  check_scan_points(Expected_SPs, scan.points);
}

TEST_F(LaserScanGeneratorTest, rightOfCecumEntranceFacingTop4beams) {
  auto pose_delta = RobotPoseDelta{
    ((Cecum_Free_X_Start + Cecum_Free_W) * Patch_Scale - 1) * map.scale(),
    (-Cecum_Patch_H * Patch_Scale + 1) * map.scale(),
    deg2rad(90)
  };
  prepare_map_and_robot_pose(pose_delta, Patch_Scale);
  const double scale = map.scale() * Patch_Scale;
  auto scan = lsg.generate_2D_laser_scan(map, rpose, 1);

  const auto Expected_SPs = ScanPoints{{map.scale(), deg2rad(-90)},
                                       {Cecum_Free_H * scale, deg2rad(0)},
                                       {Cecum_Free_W * scale, deg2rad(90)}};
  check_scan_points(Expected_SPs, scan.points);
}

TEST_F(LaserScanGeneratorTest, rightOfCecumEndFacingLeft4beams) {
  auto pose_delta = RobotPoseDelta{
    ((Cecum_Free_X_Start + Cecum_Free_W) * Patch_Scale - 1) * map.scale(),
    (Cecum_Free_Y_Start * Patch_Scale) * map.scale(),
    deg2rad(180)
  };
  prepare_map_and_robot_pose(pose_delta, Patch_Scale);
  const double scale = map.scale() * Patch_Scale;
  auto scan = lsg.generate_2D_laser_scan(map, rpose, 1);


  const auto Expected_SPs = ScanPoints{{map.scale(), deg2rad(-180)},
                                       {map.scale(), deg2rad(-90)},
                                       {Cecum_Free_W * scale, deg2rad(0)}};
  check_scan_points(Expected_SPs, scan.points);
}

TEST_F(LaserScanGeneratorTest, leftOfCecumEndFacingDown4beams) {
  auto pose_delta = RobotPoseDelta{
    (Cecum_Free_X_Start * Patch_Scale) * map.scale(),
    (Cecum_Free_Y_Start * Patch_Scale) * map.scale(),
    deg2rad(270)
  };
  prepare_map_and_robot_pose(pose_delta, Patch_Scale);
  const double scale = map.scale() * Patch_Scale;
  auto scan = lsg.generate_2D_laser_scan(map, rpose, 1);

  const auto Expected_SPs = ScanPoints{{map.scale(), deg2rad(-180)},
                                       {map.scale(), deg2rad(-90)},
                                       {Cecum_Free_W * scale, deg2rad(90)}};
  check_scan_points(Expected_SPs, scan.points);
}

TEST_F(LaserScanGeneratorTest, middleOfCecumEntranceFacingIn4beams) {
  auto pose_delta = RobotPoseDelta{
    (Cecum_Patch_W * Patch_Scale / 2) * map.scale(),
    (-Cecum_Patch_H * Patch_Scale + 1) * map.scale(),
    deg2rad(90)};
  prepare_map_and_robot_pose(pose_delta, Patch_Scale);
  auto scan = lsg.generate_2D_laser_scan(map, rpose, 1);

  /* extra 0.5*map.scale() is for robot offset inside the cell */
  const auto Expected_SPs = ScanPoints{
    {((Patch_Scale * Cecum_Free_W + 1) / 2) * map.scale(), deg2rad(-90)},
    {Patch_Scale * Cecum_Free_H * map.scale(), deg2rad(0)},
    {(Patch_Scale * Cecum_Free_W / 2 + 1) * map.scale(), deg2rad(90)}};

  check_scan_points(Expected_SPs, scan.points);
}

//------------------------------------------------------------------------------
// Misc fall facing

TEST_F(LaserScanGeneratorTest, rightOfCecumEndFacingLeftBot45deg4beams) {
  auto pose_delta = RobotPoseDelta{
    ((Cecum_Free_X_Start + Cecum_Free_W) * Patch_Scale - 1) * map.scale(),
    (Cecum_Free_Y_Start * Patch_Scale) * map.scale(),
    deg2rad(225)
  };
  prepare_map_and_robot_pose(pose_delta, Patch_Scale);
  const double scale = map.scale() * Patch_Scale;
  auto scan = lsg.generate_2D_laser_scan(map, rpose, 1);

  const auto Expected_SPs = ScanPoints{
    {map.scale(), deg2rad(-180)},
    {map.scale(), deg2rad(-90)},
    {Cecum_Free_W * scale / std::cos(deg2rad(45)), deg2rad(0)},
    {map.scale(), deg2rad(90)}};
  check_scan_points(Expected_SPs, scan.points);
}

TEST_F(LaserScanGeneratorTest, leftOfCecumEndFacingRightBot30deg4beams) {
  auto pose_delta = RobotPoseDelta{
    (Cecum_Free_X_Start * Patch_Scale) * map.scale(),
    (Cecum_Free_Y_Start * Patch_Scale) * map.scale(),
    deg2rad(-30)
  };
  prepare_map_and_robot_pose(pose_delta, Patch_Scale);
  const double scale = map.scale() * Patch_Scale;
  auto scan = lsg.generate_2D_laser_scan(map, rpose, 1);

  const auto Expected_SPs = ScanPoints{
    {map.scale(), deg2rad(-180)},
    {map.scale(), deg2rad(-90)},
    {Cecum_Free_W * scale / std::cos(deg2rad(30)), deg2rad(0)},
    {map.scale(), deg2rad(90)}};
  check_scan_points(Expected_SPs, scan.points);
}

TEST_F(LaserScanGeneratorTest, cecumCenterFacingDown8beams240FoW) {
  auto pose_delta = RobotPoseDelta{
    (Cecum_Patch_W * Patch_Scale) * map.scale() / 2,
    (Cecum_Free_Y_Start * Patch_Scale - Cecum_Free_H * Patch_Scale / 2)
      * map.scale(),
    deg2rad(-90)
  };
  prepare_map_and_robot_pose(pose_delta, Patch_Scale);
  auto lsgen = LaserScanGenerator{LaserScannerParams{15, deg2rad(270 / 8),
                                                         deg2rad(270 / 2)}};
  auto scan = lsgen.generate_2D_laser_scan(map, rpose, 1);

  constexpr double A_Step = 270 / 8;
  double left_w = (Patch_Scale * Cecum_Free_W / 2 + 1) * map.scale();
  double right_w =  ((Patch_Scale * Cecum_Free_W + 1) / 2) * map.scale();
  const auto Expected_SPs = ScanPoints{
    {left_w / std::cos(deg2rad(45 - 0 * A_Step)), deg2rad(-135 + 0 * A_Step)},
    {left_w / std::cos(deg2rad(45 - 1 * A_Step)), deg2rad(-135 + 1 * A_Step)},
    {left_w / std::cos(deg2rad(45 - 2 * A_Step)), deg2rad(-135 + 2 * A_Step)},
    {left_w / std::cos(deg2rad(45 - 3 * A_Step)), deg2rad(-135 + 3 * A_Step)},
    // 0.0 angle
    {right_w / std::cos(deg2rad(45 - 3 * A_Step)), deg2rad(-135 + 5 * A_Step)},
    {right_w / std::cos(deg2rad(45 - 2 * A_Step)), deg2rad(-135 + 6 * A_Step)},
    {right_w / std::cos(deg2rad(45 - 1 * A_Step)), deg2rad(-135 + 7 * A_Step)},
    {right_w / std::cos(deg2rad(45 - 0 * A_Step)), deg2rad(-135 + 8 * A_Step)}};
  check_scan_points(Expected_SPs, scan.points,
                    ScanPoint{map.scale(), 0.001});
}

//------------------------------------------------------------------------------

int main (int argc, char *argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
