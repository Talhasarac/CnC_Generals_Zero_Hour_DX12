"""Checks for pathlab.py - stdlib unittest, no window, no pygame display.

    python -m unittest test_pathlab -v        (from Tools/PathLab)

The point of these is the parts a screenshot cannot argue with: the A* costs, the two penalty
functions and their caps, and the book-keeping in the usage and reservation maps - which is where a
mistake is silent, because a mis-released cell only shows up as a number in a table being slightly
wrong."""

import math
import unittest

import pathlab
from pathlab import (CELL, COST_DIAGONAL, COST_ORTHOGONAL, PATH_CONGESTION_COST,
                     PATH_CONGESTION_MAX_PATHS, PATH_CROSSING_COST, PATH_CROSSING_MAX,
                     Grid, OrderStats, ReservationMap, TrafficModel, UsageMap,
                     astar, build_scenario, cell_of, compute_centroid, count_planned_crossings,
                     find_crossing_cells, make_map, order_corridor, order_individual, parse_sweep,
                     price_congestion, price_crossing, simplify_raycast, spawn_block,
                     track_reservations, tuning, world_of)


def open_grid(size: int = 21) -> Grid:
    """No border wall: the tests that want one build it themselves."""
    return Grid(size, size)


def corridor_grid(size: int = 21) -> Grid:
    """A wall down the middle with a single-cell gap - too narrow for a clearance of 1."""
    grid = open_grid(size)
    for y in range(size):
        if y != size // 2:
            grid.block(size // 2, y)
    return grid


class GeometryTests(unittest.TestCase):
    def test_world_of_is_the_centre_of_the_cell_it_names(self):
        self.assertEqual(cell_of(*world_of(4, 7)), (4, 7))
        self.assertEqual(world_of(0, 0), (CELL / 2, CELL / 2))

    def test_line_passable_sees_a_wall_the_endpoints_miss(self):
        grid = corridor_grid()
        left, right = world_of(2, 2), world_of(18, 2)
        self.assertFalse(grid.line_passable(*left, *right))
        through_gap = world_of(18, 10)
        self.assertTrue(grid.line_passable(*world_of(2, 10), *through_gap))

    def test_has_room_is_false_in_a_doorway(self):
        grid = corridor_grid()
        self.assertFalse(grid.has_room(10, 10))         # the gap itself
        self.assertTrue(grid.has_room(3, 3))            # open ground

    def test_make_map_names_the_kinds_it_knows(self):
        with self.assertRaises(ValueError) as raised:
            make_map("cliff")
        self.assertIn("cliff", str(raised.exception))
        self.assertIn("twogaps", str(raised.exception))


class AStarTests(unittest.TestCase):
    def test_straight_line_costs_one_orthogonal_step_per_cell(self):
        cells, expansions = astar(open_grid(), (2, 2), (2, 8))
        self.assertEqual(cells[0], (2, 2))
        self.assertEqual(cells[-1], (2, 8))
        self.assertEqual(len(cells), 7)
        self.assertGreater(expansions, 0)

    def test_diagonal_is_cheaper_than_going_round_the_corner(self):
        self.assertLess(COST_DIAGONAL, 2 * COST_ORTHOGONAL)
        cells, _ = astar(open_grid(), (2, 2), (6, 6))
        self.assertEqual(len(cells), 5)                 # four diagonal steps

    def test_no_path_through_a_blocked_goal(self):
        grid = open_grid()
        grid.block(5, 5)
        self.assertEqual(astar(grid, (2, 2), (5, 5))[0], [])

    def test_clearance_rejects_a_gap_a_wide_group_cannot_use(self):
        grid = corridor_grid()
        self.assertTrue(astar(grid, (2, 10), (18, 10))[0])
        self.assertEqual(astar(grid, (2, 10), (18, 10), clearance=1)[0], [])

    def test_budget_exhaustion_returns_no_path_rather_than_running_for_ever(self):
        cells, expansions = astar(open_grid(41), (2, 2), (38, 38), cell_budget=5)
        self.assertEqual(cells, [])
        self.assertLessEqual(expansions, 6)


class PricingTests(unittest.TestCase):
    def test_congestion_is_linear_then_capped(self):
        self.assertEqual(price_congestion(0), 0)
        self.assertEqual(price_congestion(2), 2 * PATH_CONGESTION_COST)
        self.assertEqual(price_congestion(PATH_CONGESTION_MAX_PATHS + 5),
                         PATH_CONGESTION_MAX_PATHS * PATH_CONGESTION_COST)

    def test_crossing_is_linear_then_capped(self):
        self.assertEqual(price_crossing(0), 0)
        self.assertEqual(price_crossing(1), PATH_CROSSING_COST)
        self.assertEqual(price_crossing(PATH_CROSSING_MAX + 5),
                         PATH_CROSSING_MAX * PATH_CROSSING_COST)

    def test_a_priced_route_goes_round_a_claimed_gap(self):
        """Four diagonal steps instead of four orthogonal ones cost 16; crossing the claimed cell
           costs 20.  So the detour has to win, and it is the cost that made it, not the map."""
        grid = open_grid()
        claimed = [(10, 9), (10, 10), (10, 11)]
        usage = UsageMap()
        usage.register(0, claimed)
        pricing = pathlab.PathPricing(usage=usage, congestion_range=1000, owner=1)
        plain, _ = astar(grid, (2, 10), (18, 10))
        priced, _ = astar(grid, (2, 10), (18, 10), pricing=pricing)
        self.assertIn((10, 10), plain)
        self.assertFalse(set(priced) & set(claimed))

    def test_tuning_hands_the_constants_back(self):
        before = (pathlab.PATH_CONGESTION_COST, pathlab.PATH_CONGESTION_MAX_PATHS,
                  pathlab.PATH_CROSSING_COST, pathlab.PATH_CROSSING_WINDOW)
        with tuning(congestion_cost=99, congestion_cap=9, crossing_cost=98, crossing_window=97):
            self.assertEqual(pathlab.PATH_CONGESTION_COST, 99)
            self.assertEqual(pathlab.PATH_CROSSING_WINDOW, 97)
        self.assertEqual((pathlab.PATH_CONGESTION_COST, pathlab.PATH_CONGESTION_MAX_PATHS,
                          pathlab.PATH_CROSSING_COST, pathlab.PATH_CROSSING_WINDOW), before)


class UsageMapTests(unittest.TestCase):
    def test_registering_twice_does_not_double_count(self):
        usage = UsageMap()
        usage.register(0, [(1, 1), (2, 2)])
        usage.register(0, [(1, 1), (3, 3)])
        self.assertEqual(usage.count, {(1, 1): 1, (3, 3): 1})

    def test_full_release_leaves_nothing_behind(self):
        usage = UsageMap()
        usage.register(0, [(1, 1), (2, 2)])
        usage.register(1, [(1, 1)])
        usage.unregister(0)
        usage.unregister(1)
        self.assertEqual(usage.count, {})

    def test_release_before_drops_only_what_is_driven_past(self):
        usage = UsageMap()
        usage.register(0, [(1, 1), (2, 2), (3, 3)])
        usage.release_before(0, 2)
        self.assertEqual(usage.count, {(3, 3): 1})
        self.assertEqual(usage.owner[0], [(3, 3)])

    def test_a_unit_is_not_charged_for_its_own_path(self):
        usage = UsageMap()
        usage.register(0, [(4, 4)])
        usage.register(1, [(4, 4)])
        self.assertEqual(usage.count_paths_on((4, 4)), 2)
        self.assertEqual(usage.count_paths_on((4, 4), exclude_owner=0), 1)


class ReservationMapTests(unittest.TestCase):
    def reserve(self, reservations, owner, arrival, heading):
        reservations.reserve(owner, [(5, 5)], [arrival], [heading])

    def test_following_traffic_is_not_a_crossing(self):
        reservations = ReservationMap()
        self.reserve(reservations, 0, 100.0, (1.0, 0.0))
        self.assertEqual(reservations.count_conflicts((5, 5), 101.0, 1, (1.0, 0.0)), 0)

    def test_opposing_traffic_at_the_same_moment_is(self):
        reservations = ReservationMap()
        self.reserve(reservations, 0, 100.0, (1.0, 0.0))
        self.assertEqual(reservations.count_conflicts((5, 5), 101.0, 1, (0.0, 1.0)), 1)

    def test_the_same_cell_much_later_is_not(self):
        reservations = ReservationMap()
        self.reserve(reservations, 0, 100.0, (1.0, 0.0))
        far = 100.0 + pathlab.PATH_CROSSING_WINDOW + 1
        self.assertEqual(reservations.count_conflicts((5, 5), far, 1, (0.0, 1.0)), 0)

    def test_release_before_keeps_the_future_and_drops_the_past(self):
        reservations = ReservationMap()
        reservations.reserve(0, [(1, 1), (2, 2)], [10.0, 30.0], [(1.0, 0.0), (1.0, 0.0)])
        reservations.release_before(0, 20.0)
        self.assertEqual(list(reservations.at), [(2, 2)])
        self.assertEqual(len(reservations.owner[0]), 1)

    def test_full_release_leaves_nothing_behind(self):
        reservations = ReservationMap()
        reservations.reserve(0, [(1, 1)], [10.0], [(1.0, 0.0)])
        reservations.reserve(1, [(1, 1)], [11.0], [(0.0, 1.0)])
        reservations.release(0)
        reservations.release(1)
        self.assertEqual(reservations.at, {})

    def test_crossing_cells_counts_an_opposed_pair_once(self):
        reservations = ReservationMap()
        self.reserve(reservations, 0, 100.0, (1.0, 0.0))
        self.reserve(reservations, 1, 101.0, (0.0, 1.0))
        self.assertEqual(find_crossing_cells(reservations), {(5, 5): 1})
        self.assertEqual(count_planned_crossings(reservations), 1)

    def test_a_queue_produces_no_crossing_cells(self):
        reservations = ReservationMap()
        self.reserve(reservations, 0, 100.0, (1.0, 0.0))
        self.reserve(reservations, 1, 101.0, (1.0, 0.0))
        self.assertEqual(find_crossing_cells(reservations), {})


class TrackTests(unittest.TestCase):
    def test_arrival_times_only_ever_grow(self):
        track = track_reservations((5.0, 5.0), [(5.0, 205.0)], start_frame=7.0)
        self.assertTrue(all(later >= earlier
                            for earlier, later in zip(track.times, track.times[1:])))
        self.assertGreaterEqual(track.times[0], 7.0)

    def test_headings_are_unit_length_and_point_along_the_leg(self):
        track = track_reservations((5.0, 5.0), [(205.0, 5.0)])
        for hx, hy in track.headings:
            self.assertAlmostEqual(math.hypot(hx, hy), 1.0, places=6)
            self.assertAlmostEqual(hx, 1.0, places=6)

    def test_every_cell_on_the_drive_line_is_claimed_once(self):
        track = track_reservations((5.0, 5.0), [(95.0, 5.0)])
        self.assertEqual(track.cells, [(cx, 0) for cx in range(1, 10)])
        self.assertEqual(len(track.times), len(track.cells))
        self.assertEqual(len(track.headings), len(track.cells))


class StraighteningTests(unittest.TestCase):
    def test_collinear_waypoints_on_open_ground_collapse(self):
        grid = open_grid()
        points = [world_of(cx, 2) for cx in range(3, 12)]
        simplified, rays = simplify_raycast(grid, world_of(2, 2), points, radius=11.0,
                                            lane_spacing=22.0)
        self.assertLess(len(simplified), len(points))
        self.assertEqual(simplified[-1], points[-1])
        self.assertTrue(rays)

    def test_a_wall_stops_the_shortcut(self):
        grid = corridor_grid()
        points = [world_of(cx, 2) for cx in range(3, 20)]
        simplified, rays = simplify_raycast(grid, world_of(2, 2), points, radius=11.0,
                                            lane_spacing=22.0)
        self.assertTrue(any(not is_clear for *_, is_clear in rays))
        self.assertEqual(simplified[-1], points[-1])


class OrderTests(unittest.TestCase):
    def plan(self, planner, map_kind="open", unit_count=6):
        grid = make_map(map_kind, seed=3)
        traffic = TrafficModel(UsageMap(), ReservationMap())
        units, orders = build_scenario(grid, unit_count, "column")
        stats = OrderStats()
        for group, goal in orders:
            planner(grid, group, goal, traffic=traffic, stats=stats, now=0.0)
        return grid, units, traffic, stats

    def test_the_corridor_gives_every_unit_its_own_goal(self):
        _, units, _, stats = self.plan(order_corridor)
        self.assertTrue(all(unit.path and unit.goal for unit in units))
        self.assertIn(stats.diameter, pathlab.GROUP_PATH_DIAMETERS)
        self.assertEqual(len({unit.goal for unit in units}), len(units))

    def test_individual_paths_claim_ground_in_both_shared_maps(self):
        _, units, traffic, _ = self.plan(order_individual)
        self.assertTrue(all(unit.cells for unit in units))
        self.assertTrue(traffic.usage.count)
        self.assertTrue(traffic.reservations.at)
        for unit in units:
            self.assertEqual(traffic.usage.owner[unit.id], unit.cells)

    def test_the_corridor_is_searched_once_for_the_whole_group(self):
        _, _, _, corridor_stats = self.plan(order_corridor)
        _, units, _, single_stats = self.plan(order_individual)
        self.assertEqual(single_stats.searches, len(units))
        self.assertLess(corridor_stats.searches, single_stats.searches)

    def test_a_group_ordered_onto_itself_says_so_instead_of_moving(self):
        grid = make_map("open", seed=3)
        traffic = TrafficModel(UsageMap(), ReservationMap())
        units = spawn_block(grid, 4, *pathlab.SPAWN_CELL)
        stats = OrderStats()
        order_corridor(grid, units, compute_centroid(units), traffic=traffic, stats=stats, now=0.0)
        self.assertTrue(stats.notes)
        self.assertFalse(any(unit.path for unit in units))


class SweepTests(unittest.TestCase):
    def test_cap_and_crossing_default_when_left_out(self):
        self.assertEqual(parse_sweep("5"),
                         [(5, PATH_CONGESTION_MAX_PATHS, 0)])

    def test_a_full_triple_is_read_in_order(self):
        self.assertEqual(parse_sweep("20/4/60,40/2/0"), [(20, 4, 60), (40, 2, 0)])

    def test_a_sweep_point_is_addressed_by_name(self):
        point = parse_sweep("20/4/60")[0]
        self.assertEqual((point.cost, point.cap, point.crossing), (20, 4, 60))


if __name__ == "__main__":
    unittest.main()
