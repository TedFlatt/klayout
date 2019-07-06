

/*

  KLayout Layout Viewer
  Copyright (C) 2006-2019 Matthias Koefferlein

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/

#include "dbNetlistCompare.h"
#include "dbNetlistDeviceClasses.h"
#include "tlProgress.h"
#include "tlTimer.h"
#include "tlEquivalenceClusters.h"
#include "tlLog.h"

//  verbose debug output
//  TODO: make this a feature?
// #define PRINT_DEBUG_NETCOMPARE

//  Add this define for case insensitive compare
//  (applies to circuits, device classes)
#define COMPARE_CASE_INSENSITIVE

namespace db
{

// --------------------------------------------------------------------------------------------------------------------
//  DeviceCompare definition and implementation

/**
 *  @brief The device compare function with "less" (operator()) and "equal" predicates
 *
 *  Device comparison is based on the equivalence of device classes (by category) and
 *  in a second step, by equivalence of the devices. The device class will implement
 *  the device equivalence function.
 */
struct DeviceCompare
{
  bool operator() (const std::pair<const db::Device *, size_t> &d1, const std::pair<const db::Device *, size_t> &d2) const
  {
    if (d1.second != d2.second) {
      return d1.second < d2.second;
    }
    return db::DeviceClass::less (*d1.first, *d2.first);
  }

  bool equals (const std::pair<const db::Device *, size_t> &d1, const std::pair<const db::Device *, size_t> &d2) const
  {
    if (d1.second != d2.second) {
      return false;
    }
    return db::DeviceClass::equal (*d1.first, *d2.first);
  }
};

// --------------------------------------------------------------------------------------------------------------------
//  SubCircuitCompare definition and implementation

/**
 *  @brief The compare function for subcircuits
 *
 *  As Subcircuits are not parametrized, the comparison of subcircuits is only based on
 *  the circuit equivalence (via category).
 */
struct SubCircuitCompare
{
  bool operator() (const std::pair<const db::SubCircuit *, size_t> &sc1, const std::pair<const db::SubCircuit *, size_t> &sc2) const
  {
    return sc1.second < sc2.second;
  }

  bool equals (const std::pair<const db::SubCircuit *, size_t> &sc1, const std::pair<const db::SubCircuit *, size_t> &sc2) const
  {
    return sc1.second == sc2.second;
  }
};

// --------------------------------------------------------------------------------------------------------------------
//  CircuitPinMapper definition and implementation

/**
 *  @brief The Circuit pin mapper handles swappable pin definitions per circuit
 *
 *  Swappable pins are implemented by mapping a pin ID to an equivalent or
 *  effective ID which is shared by all swappable pins.
 *
 *  This class manages swappable pins on a per-circuit basis.
 */
class CircuitPinMapper
{
public:
  CircuitPinMapper ()
  {
    //  .. nothing yet ..
  }

  void map_pins (const db::Circuit *circuit, size_t pin1_id, size_t pin2_id)
  {
    m_pin_map [circuit].same (pin1_id, pin2_id);
  }

  void map_pins (const db::Circuit *circuit, const std::vector<size_t> &pin_ids)
  {
    if (pin_ids.size () < 2) {
      return;
    }

    tl::equivalence_clusters<size_t> &pm = m_pin_map [circuit];
    for (size_t i = 1; i < pin_ids.size (); ++i) {
      pm.same (pin_ids [0], pin_ids [i]);
    }
  }

  size_t is_mapped (const db::Circuit *circuit, size_t pin_id) const
  {
    std::map<const db::Circuit *, tl::equivalence_clusters<size_t> >::const_iterator pm = m_pin_map.find (circuit);
    if (pm != m_pin_map.end ()) {
      return pm->second.has_attribute (pin_id);
    } else {
      return false;
    }
  }

  size_t normalize_pin_id (const db::Circuit *circuit, size_t pin_id) const
  {
    std::map<const db::Circuit *, tl::equivalence_clusters<size_t> >::const_iterator pm = m_pin_map.find (circuit);
    if (pm != m_pin_map.end ()) {
      size_t cluster_id = pm->second.cluster_id (pin_id);
      if (cluster_id > 0) {
        return (*pm->second.begin_cluster (cluster_id))->first;
      }
    }
    return pin_id;
  }

private:
  std::map<const db::Circuit *, tl::equivalence_clusters<size_t> > m_pin_map;
};

// --------------------------------------------------------------------------------------------------------------------
//  CircuitMapper definition and implementation

/**
 *  @brief A circuit mapper
 *
 *  The circuit mapper handles the circuit equivalence between the circuits of
 *  netlist A and B and also the pin mapping between the circuits from these netlists.
 *
 *  The "other" attribute will hold the circuit for the other netlist.
 *  The other methods handle pin mapping from "other" into "this" pin space.
 */
class CircuitMapper
{
public:
  CircuitMapper ()
    : mp_other (0)
  {
    //  .. nothing yet ..
  }

  void set_other (const db::Circuit *other)
  {
    mp_other = other;
  }

  const db::Circuit *other () const
  {
    return mp_other;
  }

  void map_pin (size_t this_pin, size_t other_pin)
  {
    m_pin_map.insert (std::make_pair (this_pin, other_pin));
    m_rev_pin_map.insert (std::make_pair (other_pin, this_pin));
  }

  bool has_other_pin_for_this_pin (size_t this_pin) const
  {
    return m_pin_map.find (this_pin) != m_pin_map.end ();
  }

  bool has_this_pin_for_other_pin (size_t other_pin) const
  {
    return m_rev_pin_map.find (other_pin) != m_rev_pin_map.end ();
  }

  size_t other_pin_from_this_pin (size_t this_pin) const
  {
    std::map<size_t, size_t>::const_iterator i = m_pin_map.find (this_pin);
    tl_assert (i != m_pin_map.end ());
    return i->second;
  }

  size_t this_pin_from_other_pin (size_t other_pin) const
  {
    std::map<size_t, size_t>::const_iterator i = m_rev_pin_map.find (other_pin);
    tl_assert (i != m_rev_pin_map.end ());
    return i->second;
  }

private:
  const db::Circuit *mp_other;
  std::map<size_t, size_t> m_pin_map, m_rev_pin_map;
};

// --------------------------------------------------------------------------------------------------------------------
//  DeviceFilter definition and implementation

/**
 *  @brief A device filter class
 *
 *  This class implements a device filter which is used to skip devices when
 *  generating the net graph. This is useful for stripping small caps or big
 *  resistors.
 */
class DeviceFilter
{
public:
  DeviceFilter (double cap_threshold, double res_threshold)
    : m_cap_threshold (cap_threshold), m_res_threshold (res_threshold)
  {
    //  .. nothing yet ..
  }

  bool filter (const db::Device *device) const
  {
    const db::DeviceClassResistor *res = dynamic_cast<const db::DeviceClassResistor *> (device->device_class ());
    const db::DeviceClassCapacitor *cap = dynamic_cast<const db::DeviceClassCapacitor *> (device->device_class ());

    if (res) {
      if (m_res_threshold > 0.0 && device->parameter_value (db::DeviceClassResistor::param_id_R) > m_res_threshold) {
        return false;
      }
    } else if (cap) {
      if (m_cap_threshold > 0.0 && device->parameter_value (db::DeviceClassCapacitor::param_id_C) < m_cap_threshold) {
        return false;
      }
    }

    return true;
  }

private:
  double m_cap_threshold, m_res_threshold;
};

// --------------------------------------------------------------------------------------------------------------------
//  DeviceCategorizer definition and implementation

/**
 *  @brief A device categorizer
 *
 *  The objective of this class is to supply a category ID for a given device class.
 *  The category ID also identities equivalent device classes from netlist A and B.
 */
class DeviceCategorizer
{
public:
  DeviceCategorizer ()
    : m_next_cat (0)
  {
    //  .. nothing yet ..
  }

  void same_class (const db::DeviceClass *ca, const db::DeviceClass *cb)
  {
    //  reuse existing category if one is assigned already -> this allows associating
    //  multiple categories to other ones (A->C, B->C)
    std::map<const db::DeviceClass *, size_t>::const_iterator cpa = m_cat_by_ptr.find (ca);
    std::map<const db::DeviceClass *, size_t>::const_iterator cpb = m_cat_by_ptr.find (cb);

    if (cpa != m_cat_by_ptr.end () && cpb != m_cat_by_ptr.end ()) {

      if (cpa->second != cpb->second) {
        //  join categories (cat(B)->cat(A))
        for (std::map<const db::DeviceClass *, size_t>::iterator cp = m_cat_by_ptr.begin (); cp != m_cat_by_ptr.end (); ++cp) {
          if (cp->second == cpb->second) {
            cp->second = cpa->second;
          }
        }
      }

    } else if (cpb != m_cat_by_ptr.end ()) {

      //  reuse cat(B) category
      m_cat_by_ptr.insert (std::make_pair (ca, cpb->second));

    } else if (cpa != m_cat_by_ptr.end ()) {

      //  reuse cat(A) category
      m_cat_by_ptr.insert (std::make_pair (cb, cpa->second));

    } else {

      //  new category
      ++m_next_cat;
      m_cat_by_ptr.insert (std::make_pair (ca, m_next_cat));
      m_cat_by_ptr.insert (std::make_pair (cb, m_next_cat));

    }
  }

  size_t cat_for_device (const db::Device *device)
  {
    const db::DeviceClass *cls = device->device_class ();
    if (! cls) {
      return 0;
    }

    return cat_for_device_class (cls);
  }

  bool has_cat_for_device_class (const db::DeviceClass *cls)
  {
    return m_cat_by_ptr.find (cls) != m_cat_by_ptr.end ();
  }

  size_t cat_for_device_class (const db::DeviceClass *cls)
  {
    std::map<const db::DeviceClass *, size_t>::const_iterator cp = m_cat_by_ptr.find (cls);
    if (cp != m_cat_by_ptr.end ()) {
      return cp->second;
    }

    std::string cls_name = cls->name ();
#if defined(COMPARE_CASE_INSENSITIVE)
    cls_name = tl::to_upper_case (cls_name);
#endif

    std::map<std::string, size_t>::const_iterator c = m_cat_by_name.find (cls_name);
    if (c != m_cat_by_name.end ()) {
      m_cat_by_ptr.insert (std::make_pair (cls, c->second));
      return c->second;
    } else {
      ++m_next_cat;
      m_cat_by_name.insert (std::make_pair (cls_name, m_next_cat));
      m_cat_by_ptr.insert (std::make_pair (cls, m_next_cat));
      return m_next_cat;
    }
  }

public:
  std::map<const db::DeviceClass *, size_t> m_cat_by_ptr;
  std::map<std::string, size_t> m_cat_by_name;
  size_t m_next_cat;
};

// --------------------------------------------------------------------------------------------------------------------
//  CircuitCategorizer definition and implementation

/**
 *  @brief A circuit categorizer
 *
 *  The objective of this class is to supply a category ID for a given device circuit.
 *  The category ID also identities equivalent circuit from netlist A and B.
 */
class CircuitCategorizer
{
public:
  CircuitCategorizer ()
    : m_next_cat (0)
  {
    //  .. nothing yet ..
  }

  void same_circuit (const db::Circuit *ca, const db::Circuit *cb)
  {
    ++m_next_cat;
    m_cat_by_ptr.insert (std::make_pair (ca, m_next_cat));
    m_cat_by_ptr.insert (std::make_pair (cb, m_next_cat));
  }

  size_t cat_for_subcircuit (const db::SubCircuit *subcircuit)
  {
    const db::Circuit *cr = subcircuit->circuit_ref ();
    if (! cr) {
      return 0;
    } else {
      return cat_for_circuit (cr);
    }
  }

  size_t cat_for_circuit (const db::Circuit *cr)
  {
    std::map<const db::Circuit *, size_t>::const_iterator cp = m_cat_by_ptr.find (cr);
    if (cp != m_cat_by_ptr.end ()) {
      return cp->second;
    }

    std::string cr_name = cr->name ();
#if defined(COMPARE_CASE_INSENSITIVE)
    cr_name = tl::to_upper_case (cr_name);
#endif

    std::map<std::string, size_t>::const_iterator c = m_cat_by_name.find (cr_name);
    if (c != m_cat_by_name.end ()) {
      m_cat_by_ptr.insert (std::make_pair (cr, c->second));
      return c->second;
    } else {
      ++m_next_cat;
      m_cat_by_name.insert (std::make_pair (cr_name, m_next_cat));
      m_cat_by_ptr.insert (std::make_pair (cr, m_next_cat));
      return m_next_cat;
    }
  }

public:
  std::map<const db::Circuit *, size_t> m_cat_by_ptr;
  std::map<std::string, size_t> m_cat_by_name;
  size_t m_next_cat;
};

// --------------------------------------------------------------------------------------------------------------------
//  NetGraphNode definition and implementation

static size_t translate_terminal_id (size_t tid, const db::Device *device)
{
  return device->device_class () ? device->device_class ()->normalize_terminal_id (tid) : tid;
}

/**
 *  @brief A node within the net graph
 *
 *  This class represents a node and the edges leading from this node to
 *  other nodes.
 *
 *  A graph edge is a collection of transitions, connecting terminals of
 *  devices or pins of subcircuits plus the index of node at the other end
 *  of the edge.
 *
 *  Transitions are sorted within the edge.
 */
class NetGraphNode
{
public:
  /**
   *  @brief Represents one transition within an edge_iterator
   *
   *  Each transition connects two pins of subcircuits or terminals
   *  of devices.
   *  An edge is basically a collection of transitions.
   */
  struct Transition
  {

    Transition (const db::Device *device, size_t device_category, size_t terminal1_id, size_t terminal2_id)
    {
      device_pair ().first = device;
      device_pair ().second = device_category;
      m_id1 = terminal1_id;
      m_id2 = terminal2_id;
    }

    Transition (const db::SubCircuit *subcircuit, size_t subcircuit_category, size_t pin1_id, size_t pin2_id)
    {
      subcircuit_pair ().first = subcircuit;
      subcircuit_pair ().second = subcircuit_category;
      m_id1 = std::numeric_limits<size_t>::max () - pin1_id;
      m_id2 = pin2_id;
    }

    bool operator< (const Transition &other) const
    {
      if (is_for_subcircuit () != other.is_for_subcircuit ()) {
        return is_for_subcircuit () < other.is_for_subcircuit ();
      }

      if (is_for_subcircuit ()) {

        if ((subcircuit_pair ().first != 0) != (other.subcircuit_pair ().first != 0)) {
          return (subcircuit_pair ().first != 0) < (other.subcircuit_pair ().first != 0);
        }

        if (subcircuit_pair ().first != 0) {
          SubCircuitCompare scc;
          if (! scc.equals (subcircuit_pair (), other.subcircuit_pair ())) {
            return scc (subcircuit_pair (), other.subcircuit_pair ());
          }
        }

      } else {

        if ((device_pair ().first != 0) != (other.device_pair ().first != 0)) {
          return (device_pair ().first != 0) < (other.device_pair ().first != 0);
        }

        if (device_pair ().first != 0) {
          DeviceCompare dc;
          if (! dc.equals (device_pair (), other.device_pair ())) {
            return dc (device_pair (), other.device_pair ());
          }
        }

      }

      if (m_id1 != other.m_id1) {
        return m_id1 < other.m_id1;
      }
      return m_id2 < other.m_id2;
    }

    bool operator== (const Transition &other) const
    {
      if (is_for_subcircuit () != other.is_for_subcircuit ()) {
        return false;
      }

      if (is_for_subcircuit ()) {

        if ((subcircuit_pair ().first != 0) != (other.subcircuit_pair ().first != 0)) {
          return false;
        }

        if (subcircuit_pair ().first != 0) {
          SubCircuitCompare scc;
          if (! scc.equals (subcircuit_pair (), other.subcircuit_pair ())) {
            return false;
          }
        }

      } else {

        if ((device_pair ().first != 0) != (other.device_pair ().first != 0)) {
          return false;
        }

        if (device_pair ().first != 0) {
          DeviceCompare dc;
          if (! dc.equals (device_pair (), other.device_pair ())) {
            return false;
          }
        }

      }

      return (m_id1 == other.m_id1 && m_id2 == other.m_id2);
    }

  private:
    char m_ref [sizeof (std::pair<const void *, size_t>)];
    size_t m_id1, m_id2;

    inline bool is_for_subcircuit () const
    {
      return m_id1 > std::numeric_limits<size_t>::max () / 2;
    }

    std::pair<const db::Device *, size_t> &device_pair ()
    {
      return *reinterpret_cast<std::pair<const db::Device *, size_t> *> ((void *) &m_ref);
    }

    const std::pair<const db::Device *, size_t> &device_pair () const
    {
      return *reinterpret_cast<const std::pair<const db::Device *, size_t> *> ((const void *) &m_ref);
    }

    std::pair<const db::SubCircuit *, size_t> &subcircuit_pair ()
    {
      return *reinterpret_cast<std::pair<const db::SubCircuit *, size_t> *> ((void *) &m_ref);
    }

    const std::pair<const db::SubCircuit *, size_t> &subcircuit_pair () const
    {
      return *reinterpret_cast<const std::pair<const db::SubCircuit *, size_t> *> ((const void *) &m_ref);
    }
  };

  struct EdgeToEdgeOnlyCompare
  {
    bool operator() (const std::pair<std::vector<Transition>, std::pair<size_t, const db::Net *> > &a, const std::vector<Transition> &b) const
    {
      return a.first < b;
    }
  };

  typedef std::vector<std::pair<std::vector<Transition>, std::pair<size_t, const db::Net *> > >::const_iterator edge_iterator;

  NetGraphNode (const db::Net *net, DeviceCategorizer &device_categorizer, CircuitCategorizer &circuit_categorizer, const DeviceFilter &device_filter, const std::map<const db::Circuit *, CircuitMapper> *circuit_map, const CircuitPinMapper *pin_map)
    : mp_net (net), m_other_net_index (std::numeric_limits<size_t>::max ())
  {
    if (! net) {
      return;
    }

    std::map<const db::Net *, size_t> n2entry;

    for (db::Net::const_subcircuit_pin_iterator i = net->begin_subcircuit_pins (); i != net->end_subcircuit_pins (); ++i) {

      const db::SubCircuit *sc = i->subcircuit ();
      size_t pin_id = i->pin ()->id ();
      const db::Circuit *cr = sc->circuit_ref ();

      size_t this_pin_id = pin_id;

      if (! cr->net_for_pin (pin_id)) {

        //  fallback (e.g. when abstract circuits are addressed: just include a transition to 0
        //  to make the net distinguishable from a net without this connection.
        Transition ed (sc, circuit_categorizer.cat_for_subcircuit (sc), pin_id, pin_id);

        std::map<const db::Net *, size_t>::const_iterator in = n2entry.find (0);
        if (in == n2entry.end ()) {
          in = n2entry.insert (std::make_pair ((const db::Net *) 0, m_edges.size ())).first;
          m_edges.push_back (std::make_pair (std::vector<Transition> (), std::make_pair (size_t (0), (const db::Net *) 0)));
        }

        m_edges [in->second].first.push_back (ed);

        continue;

      }

      std::map<const db::Circuit *, CircuitMapper>::const_iterator icm = circuit_map->find (cr);
      if (icm == circuit_map->end ()) {
        //  this can happen if the other circuit is not present - this is allowed for single-pin
        //  circuits.
        continue;
      }

      const CircuitMapper *cm = & icm->second;

      //  A pin assignment may be missing because there is no net for a pin -> skip this

      if (! cm->has_other_pin_for_this_pin (pin_id)) {
        continue;
      }

      //  NOTE: if cm is given, cr and pin_id are given in terms of the canonical "other" circuit.
      //  For c1 this is the c1->c2 mapper, for c2 this is the c2->c2 dummy mapper.

      cr = cm->other ();
      pin_id = cm->other_pin_from_this_pin (pin_id);

      //  realize pin swapping by normalization of pin ID

      pin_id = pin_map->normalize_pin_id (cr, pin_id);

      //  we cannot afford creating edges from all to all other pins, so we just create edges to the previous and next
      //  pin. This may take more iterations to solve, but should be equivalent.

      size_t pin_count = cr->pin_count ();

      //  take a number if additional pins as edges: this allows identifying a pin as dependent
      //  from other pins hence nets are propagated. We assume that there are 4 power pins max so
      //  5 additional pins should be sufficient to capture one additional non-power pin.

      size_t take_additional_pins = 5;

      std::vector<size_t> pids;
      pids.reserve (take_additional_pins + 1);
      //  this symmetrizes the pin list with respect to the before-normalization pin id:
      pids.push_back (pin_id);

      for (size_t n = 0; n < take_additional_pins; ++n) {
        size_t add_pin_id = (pin_id + n + 1) % pin_count;
        if (add_pin_id == pin_id) {
          break;
        }
        if (cm->has_this_pin_for_other_pin (add_pin_id)
             //  NOTE: we do not include transitions to equivalent pins in our graph intentionally.
             //  Reasoning: for abstract circuits, transitions are basically useless. For more than
             //  two equivalent pins, the transitions are unpredictable.
             && pin_map->normalize_pin_id (cr, cm->this_pin_from_other_pin (add_pin_id)) != pin_id) {
          pids.push_back (add_pin_id);
        } else {
          //  skip pins without mapping
          ++take_additional_pins;
        }
      }

      for (std::vector<size_t>::const_iterator i = pids.begin (); i != pids.end (); ++i) {

        size_t pin2_id = *i;
        size_t this_pin2_id = cm->this_pin_from_other_pin (pin2_id);

        if (this_pin2_id == this_pin_id) {
          //  we should not go back to our original, non-normalized pin
          continue;
        }

        //  NOTE: if a pin mapping is given, EdgeDesc::pin1_id and EdgeDesc::pin2_id are given
        //  as pin ID's of the other circuit.
        Transition ed (sc, circuit_categorizer.cat_for_subcircuit (sc), pin_id, pin_map->normalize_pin_id (cr, pin2_id));

        const db::Net *net2 = sc->net_for_pin (this_pin2_id);

        std::map<const db::Net *, size_t>::const_iterator in = n2entry.find (net2);
        if (in == n2entry.end ()) {
          in = n2entry.insert (std::make_pair (net2, m_edges.size ())).first;
          m_edges.push_back (std::make_pair (std::vector<Transition> (), std::make_pair (size_t (0), net2)));
        }

        m_edges [in->second].first.push_back (ed);

      }

    }

    for (db::Net::const_terminal_iterator i = net->begin_terminals (); i != net->end_terminals (); ++i) {

      const db::Device *d = i->device ();
      if (! device_filter.filter (d)) {
        continue;
      }

      size_t device_cat = device_categorizer.cat_for_device (d);
      size_t terminal1_id = translate_terminal_id (i->terminal_id (), d);

      const std::vector<db::DeviceTerminalDefinition> &td = d->device_class ()->terminal_definitions ();
      for (std::vector<db::DeviceTerminalDefinition>::const_iterator it = td.begin (); it != td.end (); ++it) {

        if (it->id () != i->terminal_id ()) {

          size_t terminal2_id = translate_terminal_id (it->id (), d);
          Transition ed2 (d, device_cat, terminal1_id, terminal2_id);

          const db::Net *net2 = d->net_for_terminal (it->id ());

          std::map<const db::Net *, size_t>::const_iterator in = n2entry.find (net2);
          if (in == n2entry.end ()) {
            in = n2entry.insert (std::make_pair (net2, m_edges.size ())).first;
            m_edges.push_back (std::make_pair (std::vector<Transition> (), std::make_pair (size_t (0), net2)));
          }

          m_edges [in->second].first.push_back (ed2);

        }

      }

    }
  }

  const db::Net *net () const
  {
    return mp_net;
  }

  bool has_other () const
  {
    return m_other_net_index != std::numeric_limits<size_t>::max ();
  }

  size_t other_net_index () const
  {
    return m_other_net_index;
  }

  void set_other_net (size_t index)
  {
    m_other_net_index = index;
  }

  void unset_other_net ()
  {
    m_other_net_index = std::numeric_limits<size_t>::max ();
  }

  bool empty () const
  {
    return m_edges.empty ();
  }

  void apply_net_index (const std::map<const db::Net *, size_t> &ni)
  {
    for (std::vector<std::pair<std::vector<Transition>, std::pair<size_t, const db::Net *> > >::iterator i = m_edges.begin (); i != m_edges.end (); ++i) {
      std::map<const db::Net *, size_t>::const_iterator j = ni.find (i->second.second);
      tl_assert (j != ni.end ());
      i->second.first = j->second;
    }

    //  "deep sorting" of the edge descriptor
    for (std::vector<std::pair<std::vector<Transition>, std::pair<size_t, const db::Net *> > >::iterator i = m_edges.begin (); i != m_edges.end (); ++i) {
      std::sort (i->first.begin (), i->first.end ());
    }

    std::sort (m_edges.begin (), m_edges.end ());
  }

  bool operator< (const NetGraphNode &node) const
  {
    if (m_edges.size () != node.m_edges.size ()) {
      return m_edges.size () < node.m_edges.size ();
    }
    for (size_t i = 0; i < m_edges.size (); ++i) {
      if (m_edges [i].first != node.m_edges [i].first) {
        return m_edges [i].first < node.m_edges [i].first;
      }
    }
    if (m_edges.empty ()) {
      //  do a more detailed analysis on the edges
      return edge_less (net (), node.net ());
    }
    return false;
  }

  bool operator== (const NetGraphNode &node) const
  {
    if (m_edges.size () != node.m_edges.size ()) {
      return false;
    }
    for (size_t i = 0; i < m_edges.size (); ++i) {
      if (m_edges [i].first != node.m_edges [i].first) {
        return false;
      }
    }
    if (m_edges.empty ()) {
      //  do a more detailed analysis on the edges
      return edge_equal (net (), node.net ());
    }
    return true;
  }

  void swap (NetGraphNode &other)
  {
    std::swap (m_other_net_index, other.m_other_net_index);
    std::swap (mp_net, other.mp_net);
    m_edges.swap (other.m_edges);
  }

  edge_iterator begin () const
  {
    return m_edges.begin ();
  }

  edge_iterator end () const
  {
    return m_edges.end ();
  }

  edge_iterator find_edge (const std::vector<Transition> &edge) const
  {
    edge_iterator res = std::lower_bound (begin (), end (), edge, NetGraphNode::EdgeToEdgeOnlyCompare ());
    if (res == end () || res->first != edge) {
      return end ();
    } else {
      return res;
    }
  }

private:
  const db::Net *mp_net;
  size_t m_other_net_index;
  std::vector<std::pair<std::vector<Transition>, std::pair<size_t, const db::Net *> > > m_edges;

  /**
   *  @brief Compares edges as "less"
   *  Edge comparison is based on the pins attached (name of the first pin).
   */
  static bool edge_less (const db::Net *a, const db::Net *b)
  {
    if ((a != 0) != (b != 0)) {
      return (a != 0) < (b != 0);
    }
    if (a != 0) {
      if (a->pin_count () != b->pin_count ()) {
        return a->pin_count () < b->pin_count ();
      }
      if (a->pin_count () > 0) {
        const std::string &pna = a->begin_pins ()->pin ()->name ();
        const std::string &pnb = b->begin_pins ()->pin ()->name ();
        if (! pna.empty () && ! pnb.empty ()) {
          return pna < pnb;
        }
      }
      return false;
    } else {
      return false;
    }
  }

  /**
   *  @brief Compares edges as "equal"
   *  See edge_less for the comparison details.
   */
  static bool edge_equal (const db::Net *a, const db::Net *b)
  {
    if ((a != 0) != (b != 0)) {
      return false;
    }
    if (a != 0) {
      if (a->pin_count () != b->pin_count ()) {
        return false;
      }
      if (a->pin_count () > 0) {
        const std::string &pna = a->begin_pins ()->pin ()->name ();
        const std::string &pnb = b->begin_pins ()->pin ()->name ();
        if (! pna.empty () && ! pnb.empty ()) {
          return pna == pnb;
        }
      }
      return true;
    } else {
      return true;
    }
  }
};

// --------------------------------------------------------------------------------------------------------------------
//  NetGraph definition and implementation

}

namespace std
{
  void swap (db::NetGraphNode &a, db::NetGraphNode &b)
  {
    a.swap (b);
  }
}

namespace db
{

struct CompareNodePtr
{
  bool operator() (const NetGraphNode *a, const NetGraphNode *b) const
  {
    return *a < *b;
  }
};

class TentativeNodeMapping;

/**
 *  @brief The net graph for the compare algorithm
 */
class NetGraph
{
public:
  typedef std::vector<NetGraphNode>::const_iterator node_iterator;

  NetGraph ()
  {
    //  .. nothing yet ..
  }

  /**
   *  @brief Builds the net graph
   */
  void build (const db::Circuit *c, DeviceCategorizer &device_categorizer, CircuitCategorizer &circuit_categorizer, const db::DeviceFilter &device_filter, const std::map<const db::Circuit *, CircuitMapper> *circuit_and_pin_mapping, const CircuitPinMapper *circuit_pin_mapper);

  /**
   *  @brief Gets the node index for the given net
   */
  size_t node_index_for_net (const db::Net *net) const
  {
    std::map<const db::Net *, size_t>::const_iterator j = m_net_index.find (net);
    tl_assert (j != m_net_index.end ());
    return j->second;
  }

  /**
   *  @brief Gets the node for a given node index
   */
  const db::NetGraphNode &node (size_t net_index) const
  {
    return m_nodes [net_index];
  }

  /**
   *  @brief Gets the node for a given node index (non-const version)
   */
  db::NetGraphNode &node (size_t net_index)
  {
    return m_nodes [net_index];
  }

  /**
   *  @brief Gets the net for a given node index
   */
  const db::Net *net_by_node_index (size_t net_index) const
  {
    return m_nodes [net_index].net ();
  }

  /**
   *  @brief Establishes an equivalence between two nodes of netlist A (this) and B (other)
   */
  void identify (size_t net_index, size_t other_net_index)
  {
    m_nodes [net_index].set_other_net (other_net_index);
  }

  /**
   *  @brief Removes the equivalence from the node with the given index
   */
  void unidentify (size_t net_index)
  {
    m_nodes [net_index].unset_other_net ();
  }

  /**
   *  @brief Iterator over the nodes in this graph (begin)
   */
  node_iterator begin () const
  {
    return m_nodes.begin ();
  }

  /**
   *  @brief Iterator over the nodes in this graph (end)
   */
  node_iterator end () const
  {
    return m_nodes.end ();
  }

  /**
   *  @brief The circuit this graph is associated with
   */
  const db::Circuit *circuit () const
  {
    return mp_circuit;
  }

  /**
   *  @brief Implementation of the backtracking algorithm
   *
   *  This method derives new node assignments based on the (proposed)
   *  identity of nodes this->[net_index] and other[node].
   *  The return value will be:
   *
   *   >0    if node identity could be established. The return value
   *         is the number of new node pairs established. All
   *         node pairs (including the initial proposed identity)
   *         are assigned.
   *   ==0   identity could be established. No more assignments are made.
   *   max   no decision could be made because the max. complexity
   *         was exhausted. No assignments were made.
   *
   *  (here: max=max of size_t). The complexity is measured in
   *  backtracking depth (number of graph jumps) and decision tree
   *  branching complexity N (="n_branch", means: N*N decisions to be made).
   *
   *  If "tentative" is non-null, assignments will be recorded in the TentativeMapping
   *  audit object and can be undone afterwards when backtracking recursion happens.
   *
   *  If "with_ambiguous" is true, ambiguous matches are considered. This will happen
   *  in a second pass to give exact and unique matches priority over ambiguous matches.
   */
  size_t derive_node_identities (size_t net_index, NetGraph &other, size_t depth, size_t max_depth, size_t n_branch, size_t max_n_branch, NetlistCompareLogger *logger, CircuitPinMapper *circuit_pin_mapper, TentativeNodeMapping *tentative, bool with_ambiguous);

  /**
   *  @brief The backtracking driver
   *
   *  This method will analyze the given nodes and call "derive_node_identities" for all nodes
   *  with a proposed identity. With "with_ambiguous", amiguities are resolved by trying
   *  different combinations in tentative mode and deciding for one combination if possible.
   */
  size_t derive_node_identities_from_node_set (const std::vector<const NetGraphNode *> &nodes, const std::vector<const NetGraphNode *> &other_nodes, NetGraph &other, size_t depth, size_t max_depth, size_t n_branch, size_t max_n_branch, NetlistCompareLogger *logger, CircuitPinMapper *circuit_pin_mapper, TentativeNodeMapping *tentative, bool with_ambiguous);

private:
  std::vector<NetGraphNode> m_nodes;
  std::map<const db::Net *, size_t> m_net_index;
  const db::Circuit *mp_circuit;
};

// --------------------------------------------------------------------------------------------------------------------

/**
 *  @brief Represents an interval of NetGraphNode objects in a node set
 */
struct NodeRange
{
  NodeRange (size_t _num, std::vector<const NetGraphNode *>::const_iterator _n1, std::vector<const NetGraphNode *>::const_iterator _nn1, std::vector<const NetGraphNode *>::const_iterator _n2, std::vector<const NetGraphNode *>::const_iterator _nn2)
    : num (_num), n1 (_n1), nn1 (_nn1), n2 (_n2), nn2 (_nn2)
  {
    //  .. nothing yet ..
  }

  bool operator< (const NodeRange &other) const
  {
    return num < other.num;
  }

  size_t num;
  std::vector<const NetGraphNode *>::const_iterator n1, nn1, n2, nn2;
};

// --------------------------------------------------------------------------------------------------------------------

/**
 *  @brief An audit object which allows reverting tentative node assignments
 */
class TentativeNodeMapping
{
public:
  TentativeNodeMapping (NetGraph *g1, NetGraph *g2)
    : mp_g1 (g1), mp_g2 (g2)
  { }

  ~TentativeNodeMapping ()
  {
    for (std::vector<std::pair<size_t, size_t> >::const_iterator i = m_to_undo.begin (); i != m_to_undo.end (); ++i) {
      mp_g1->unidentify (i->first);
      mp_g2->unidentify (i->second);
    }
  }

  static void map_pair (TentativeNodeMapping *nm, NetGraph *g1, size_t n1, NetGraph *g2, size_t n2)
  {
    g1->identify (n1, n2);
    g2->identify (n2, n1);
    if (nm) {
      nm->keep (n1, n2);
    }
  }

private:
  std::vector<std::pair<size_t, size_t> > m_to_undo;
  NetGraph *mp_g1, *mp_g2;

  void keep (size_t n1, size_t n2)
  {
    m_to_undo.push_back (std::make_pair (n1, n2));
  }
};

// --------------------------------------------------------------------------------------------------------------------
//  NetGraph implementation

void
NetGraph::build (const db::Circuit *c, DeviceCategorizer &device_categorizer, CircuitCategorizer &circuit_categorizer, const db::DeviceFilter &device_filter, const std::map<const db::Circuit *, CircuitMapper> *circuit_and_pin_mapping, const CircuitPinMapper *circuit_pin_mapper)
{
  tl::SelfTimer timer (tl::verbosity () >= 31, tl::to_string (tr ("Building net graph for circuit: ")) + c->name ());

  mp_circuit = c;

  m_nodes.clear ();
  m_net_index.clear ();

  //  create a dummy node for a null net
  m_nodes.push_back (NetGraphNode (0, device_categorizer, circuit_categorizer, device_filter, circuit_and_pin_mapping, circuit_pin_mapper));

  size_t nets = 0;
  for (db::Circuit::const_net_iterator n = c->begin_nets (); n != c->end_nets (); ++n) {
    ++nets;
  }
  m_nodes.reserve (nets);

  for (db::Circuit::const_net_iterator n = c->begin_nets (); n != c->end_nets (); ++n) {
    NetGraphNode node (n.operator-> (), device_categorizer, circuit_categorizer, device_filter, circuit_and_pin_mapping, circuit_pin_mapper);
    if (! node.empty () || n->pin_count () > 0) {
      m_nodes.push_back (node);
    }
  }

  std::sort (m_nodes.begin (), m_nodes.end ());

  for (std::vector<NetGraphNode>::const_iterator i = m_nodes.begin (); i != m_nodes.end (); ++i) {
    m_net_index.insert (std::make_pair (i->net (), i - m_nodes.begin ()));
  }
  for (std::vector<NetGraphNode>::iterator i = m_nodes.begin (); i != m_nodes.end (); ++i) {
    i->apply_net_index (m_net_index);
  }
}

size_t
NetGraph::derive_node_identities (size_t net_index, NetGraph &other, size_t depth, size_t max_depth, size_t n_branch, size_t max_n_branch, NetlistCompareLogger *logger, CircuitPinMapper *circuit_pin_mapper, TentativeNodeMapping *tentative, bool with_ambiguous)
{
  NetGraphNode *n = & node (net_index);
  NetGraphNode *nother = & other.node (n->other_net_index ());

#if defined(PRINT_DEBUG_NETCOMPARE)
  std::string indent;
  for (size_t d = 0; d < depth; ++d) {
    indent += " ";
  }
#endif

#if defined(PRINT_DEBUG_NETCOMPARE)
  if (! tentative) {
    tl::info << indent << "deducing from pair: " << n->net ()->expanded_name () << " vs. " << nother->net ()->expanded_name ();
  }
#endif

  size_t new_nodes = 0;

  //  non-ambiguous paths to non-assigned nodes create a node identity on the
  //  end of this path

  for (NetGraphNode::edge_iterator e = n->begin (); e != n->end (); ) {

    NetGraphNode::edge_iterator ee = e;
    ++ee;

    while (ee != n->end () && ee->first == e->first) {
      ++ee;
    }

    std::vector<const NetGraphNode *> nodes;
    nodes.reserve (ee - e);

    std::vector<const NetGraphNode *> other_nodes;
    other_nodes.reserve (ee - e);

    for (NetGraphNode::edge_iterator i = e; i != ee; ++i) {
      const NetGraphNode *n = &node (i->second.first);
      nodes.push_back (n);
    }

    if (! nodes.empty ()) {   //  if non-ambiguous, non-assigned

      NetGraphNode::edge_iterator e_other = nother->find_edge (e->first);
      if (e_other != nother->end ()) {

        NetGraphNode::edge_iterator ee_other = e_other;
        ++ee_other;

        while (ee_other != nother->end () && ee_other->first == e_other->first) {
          ++ee_other;
        }

        size_t count_other = 0;
        for (NetGraphNode::edge_iterator i = e_other; i != ee_other && count_other < 2; ++i) {

          const NetGraphNode *n = &other.node (i->second.first);
          other_nodes.push_back (n);

        }

      }

    }

    if (! nodes.empty () || ! other_nodes.empty ()) {

      std::sort (nodes.begin (), nodes.end (), CompareNodePtr ());
      std::sort (other_nodes.begin (), other_nodes.end (), CompareNodePtr ());

      //  for the purpose of match evaluation we require an exact match of the node structure

      if (tentative) {

        if (nodes.size () != other_nodes.size ()) {
          return std::numeric_limits<size_t>::max ();
        }

        //  1:1 pairing is less strict
        if (nodes.size () > 1 || other_nodes.size () > 1) {
          for (size_t i = 0; i < nodes.size (); ++i) {
            if (! (*nodes[i] == *other_nodes[i])) {
              return std::numeric_limits<size_t>::max ();
            }
          }
        }

      }

      //  propagate pairing in picky mode: this means we only accept exact a match if the node set
      //  is exactly identical and no ambiguous nodes are present when ambiguous nodes are forbidden

      size_t bt_count = derive_node_identities_from_node_set (nodes, other_nodes, other, depth, max_depth, n_branch, max_n_branch, logger, circuit_pin_mapper, tentative, with_ambiguous);

      if (bt_count == std::numeric_limits<size_t>::max ()) {
        if (tentative) {
          return bt_count;
        }
      } else {
        new_nodes += bt_count;
      }

    }

    e = ee;

  }

#if defined(PRINT_DEBUG_NETCOMPARE)
  if (! tentative && new_nodes > 0) {
    tl::info << indent << "finished pair deduction: " << n->net ()->expanded_name () << " vs. " << nother->net ()->expanded_name () << " with " << new_nodes << " new pairs";
  }
#endif

  return new_nodes;
}

size_t
NetGraph::derive_node_identities_from_node_set (const std::vector<const NetGraphNode *> &nodes, const std::vector<const NetGraphNode *> &other_nodes, NetGraph &other, size_t depth, size_t max_depth, size_t n_branch, size_t max_n_branch, NetlistCompareLogger *logger, CircuitPinMapper *circuit_pin_mapper, TentativeNodeMapping *tentative, bool with_ambiguous)
{
#if defined(PRINT_DEBUG_NETCOMPARE)
  std::string indent;
  for (size_t d = 0; d < depth; ++d) {
    indent += " ";
  }
  indent += "*" + tl::to_string (n_branch) + " ";
#endif

  size_t new_nodes = 0;

  if (depth > max_depth) {
#if defined(PRINT_DEBUG_NETCOMPARE)
    tl::info << indent << "max. depth exhausted (" << depth + 1 << ">" << max_depth << ")";
#endif
    return std::numeric_limits<size_t>::max ();
  }

  if (nodes.size () == 1 && other_nodes.size () == 1) {

    if (! nodes.front ()->has_other () && ! other_nodes.front ()->has_other ()) {

      //  a single candiate: just take this one -> this may render
      //  inexact matches, but further propagates net pairing

      size_t ni = node_index_for_net (nodes.front ()->net ());
      size_t other_ni = other.node_index_for_net (other_nodes.front ()->net ());

      TentativeNodeMapping::map_pair (tentative, this, ni, &other, other_ni);

#if defined(PRINT_DEBUG_NETCOMPARE)
      tl::info << indent << "deduced match (singular): " << nodes.front ()->net ()->expanded_name () << " vs. " << other_nodes.front ()->net ()->expanded_name ();
#endif
      if (logger && ! tentative) {
        if (! (node (ni) == other.node (other_ni))) {
          //  this is a mismatch but we continue, because that is the only candidate
          logger->net_mismatch (nodes.front ()->net (), other_nodes.front ()->net ());
        } else {
          logger->match_nets (nodes.front ()->net (), other_nodes.front ()->net ());
        }
      }

      //  continue here.
      size_t bt_count = derive_node_identities (ni, other, depth + 1, max_depth, n_branch, max_n_branch, logger, circuit_pin_mapper, tentative, with_ambiguous);

      if (bt_count != std::numeric_limits<size_t>::max ()) {
        new_nodes += bt_count;
      } else if (tentative) {
        return bt_count;
      }

      new_nodes += 1;

    } else if (nodes.front ()->has_other ()) {

      //  this decision leads to a contradiction
      if (other.node_index_for_net (other_nodes.front ()->net ()) != nodes.front ()->other_net_index ()) {
        return std::numeric_limits<size_t>::max ();
      }

    } else {

      //  mismatch of assignment state
      return std::numeric_limits<size_t>::max ();

    }

    return new_nodes;

  }

  //  Determine the range of nodes with same identity

  std::vector<NodeRange> node_ranges;

  std::vector<const NetGraphNode *>::const_iterator n1 = nodes.begin ();
  std::vector<const NetGraphNode *>::const_iterator n2 = other_nodes.begin ();

  while (n1 != nodes.end () && n2 != other_nodes.end ()) {

    if ((*n1)->has_other ()) {
      ++n1;
      continue;
    } else if ((*n2)->has_other ()) {
      ++n2;
      continue;
    }

    if (**n1 < **n2) {
      ++n1;
      continue;
    } else if (**n2 < **n1) {
      ++n2;
      continue;
    }

    std::vector<const NetGraphNode *>::const_iterator nn1 = n1, nn2 = n2;

    size_t num = 1;
    ++nn1;
    ++nn2;
    while (nn1 != nodes.end () && nn2 != other_nodes.end ()) {
      if ((*nn1)->has_other ()) {
        ++nn1;
      } else if ((*nn2)->has_other ()) {
        ++nn2;
      } else if (! (**nn1 == **n1) || ! (**nn2 == **n2)) {
        break;
      } else {
        ++num;
        ++nn1;
        ++nn2;
      }
    }

    if (num == 1 || with_ambiguous) {
      node_ranges.push_back (NodeRange (num, n1, nn1, n2, nn2));
    }

    //  in tentative mode ambiguous nodes don't make a match without
    //  with_ambiguous
    if (num > 1 && tentative && ! with_ambiguous) {
      return std::numeric_limits<size_t>::max ();
    }

    n1 = nn1;
    n2 = nn2;

  }

  if (with_ambiguous) {
    std::stable_sort (node_ranges.begin (), node_ranges.end ());
  }

  for (std::vector<NodeRange>::iterator nr = node_ranges.begin (); nr != node_ranges.end (); ++nr) {

    //  node ranges might have changed - adjust to real count and skip leading pairs assigned already

    while (nr->n1 != nr->nn1 && nr->n2 != nr->nn2) {
      if ((*nr->n1)->has_other ()) {
        ++nr->n1;
      } else if ((*nr->n2)->has_other ()) {
        ++nr->n2;
      } else {
        break;
      }
    }

    nr->num = 0;
    std::vector<const NetGraphNode *>::const_iterator i1 = nr->n1, i2 = nr->n2;

    while (i1 != nr->nn1 && i2 != nr->nn2) {
      if ((*i1)->has_other ()) {
        ++i1;
      } else if ((*i2)->has_other ()) {
        ++i2;
      } else {
        ++nr->num;
        ++i1;
        ++i2;
      }
    }

    if (nr->num < 1) {

      //  ignore this - it got obsolete.

    } else if (nr->num == 1) {

      if (! (*nr->n1)->has_other () && ! (*nr->n2)->has_other ()) {

        //  A single candiate: just take this one -> this may render
        //  inexact matches, but further propagates net pairing

        size_t ni = node_index_for_net ((*nr->n1)->net ());
        size_t other_ni = other.node_index_for_net ((*nr->n2)->net ());

        TentativeNodeMapping::map_pair (tentative, this, ni, &other, other_ni);

#if defined(PRINT_DEBUG_NETCOMPARE)
        tl::info << indent << "deduced match (singular): " << (*nr->n1)->net ()->expanded_name () << " vs. " << (*nr->n2)->net ()->expanded_name ();
#endif
        if (logger && ! tentative) {
          if (! (node (ni) == other.node (other_ni))) {
            //  this is a mismatch, but we continue with this
            logger->net_mismatch ((*nr->n1)->net (), (*nr->n2)->net ());
          } else {
            logger->match_nets ((*nr->n1)->net (), (*nr->n2)->net ());
          }
        }

        //  continue here.
        size_t bt_count = derive_node_identities (ni, other, depth + 1, max_depth, n_branch, max_n_branch, logger, circuit_pin_mapper, tentative, with_ambiguous);

        if (bt_count != std::numeric_limits<size_t>::max ()) {
          new_nodes += bt_count;
          new_nodes += 1;
        } else if (tentative) {
          new_nodes = bt_count;
        }

      } else if ((*nr->n1)->has_other ()) {

        //  this decision leads to a contradiction
        if (other.node_index_for_net ((*nr->n2)->net ()) != (*nr->n1)->other_net_index ()) {
          return std::numeric_limits<size_t>::max ();
        }

      } else {

        //  mismatch of assignment state
        return std::numeric_limits<size_t>::max ();

      }

    } else if (nr->num * n_branch > max_n_branch) {

#if defined(PRINT_DEBUG_NETCOMPARE)
      tl::info << indent << "max. complexity exhausted (" << nr->num << "*" << n_branch << ">" << max_n_branch << ") - mismatch.";
#endif
      return std::numeric_limits<size_t>::max ();

    } else {

#if defined(PRINT_DEBUG_NETCOMPARE)
      tl::info << indent << "analyzing ambiguity group with " << nr->num << " members";
#endif
      std::vector<std::pair<const NetGraphNode *, const NetGraphNode *> > pairs;
      tl::equivalence_clusters<const NetGraphNode *> equivalent_other_nodes;
      std::set<const NetGraphNode *> seen;

      for (std::vector<const NetGraphNode *>::const_iterator i1 = nr->n1; i1 != nr->nn1; ++i1) {

        if ((*i1)->has_other ()) {
          continue;
        }

        bool any = false;

        std::vector<const NetGraphNode *>::const_iterator i2;
        for (i2 = nr->n2; i2 != nr->nn2; ++i2) {

          if ((*i2)->has_other ()) {
            continue;
          }

          if (seen.find (*i2) != seen.end ()) {
            continue;
          }

          size_t ni = node_index_for_net ((*i1)->net ());
          size_t other_ni = other.node_index_for_net ((*i2)->net ());

          TentativeNodeMapping tn (this, &other);
          TentativeNodeMapping::map_pair (&tn, this, ni, &other, other_ni);

          //  try this candidate in tentative mode
#if defined(PRINT_DEBUG_NETCOMPARE)
          tl::info << indent << "trying in tentative mode: " << (*i1)->net ()->expanded_name () << " vs. " << (*i2)->net ()->expanded_name ();
#endif
          size_t bt_count = derive_node_identities (ni, other, depth + 1, max_depth, nr->num * n_branch, max_n_branch, logger, circuit_pin_mapper, &tn, with_ambiguous);

          if (bt_count != std::numeric_limits<size_t>::max ()) {

#if defined(PRINT_DEBUG_NETCOMPARE)
            tl::info << indent << "match found";
#endif
            //  we have a match ...

            if (any) {

              //  there is already a known pair, so we can mark *i2 and the previous *i2 as equivalent
              //  (makes them ambiguous)
              equivalent_other_nodes.same (*i2, pairs.back ().second);

            } else {

              //  identified a new pair
              new_nodes += bt_count + 1;
              pairs.push_back (std::make_pair (*i1, *i2));
              seen.insert (*i2);
              any = true;

            }

          }

        }

        if (! any && tentative) {
#if defined(PRINT_DEBUG_NETCOMPARE)
          tl::info << indent << "mismatch.";
#endif
          //  a mismatch - stop here.
          return std::numeric_limits<size_t>::max ();
        }

      }

      if (! tentative) {

        //  issue the matching pairs

        for (std::vector<std::pair<const NetGraphNode *, const NetGraphNode *> >::const_iterator p = pairs.begin (); p != pairs.end (); ++p) {

          size_t ni = node_index_for_net (p->first->net ());
          size_t other_ni = other.node_index_for_net (p->second->net ());

          TentativeNodeMapping::map_pair (tentative, this, ni, &other, other_ni);

#if defined(PRINT_DEBUG_NETCOMPARE)
          tl::info << indent << "deduced match: " << p->first->net ()->expanded_name () << " vs. " << p->second->net ()->expanded_name ();
#endif
          if (logger) {
            bool ambiguous = equivalent_other_nodes.has_attribute (p->second);
            if (ambiguous) {
              logger->match_ambiguous_nets (p->first->net (), p->second->net ());
            } else {
              logger->match_nets (p->first->net (), p->second->net ());
            }
          }

        }

        //  And seek further from these pairs

        for (std::vector<std::pair<const NetGraphNode *, const NetGraphNode *> >::const_iterator p = pairs.begin (); p != pairs.end (); ++p) {

          size_t ni = node_index_for_net (p->first->net ());

          size_t bt_count = derive_node_identities (ni, other, depth + 1, max_depth, nr->num * n_branch, max_n_branch, logger, circuit_pin_mapper, tentative, with_ambiguous);
          tl_assert (bt_count != std::numeric_limits<size_t>::max ());

        }

      } else {

        for (std::vector<std::pair<const NetGraphNode *, const NetGraphNode *> >::const_iterator p = pairs.begin (); p != pairs.end (); ++p) {

          size_t ni = node_index_for_net (p->first->net ());
          size_t other_ni = other.node_index_for_net (p->second->net ());

          TentativeNodeMapping::map_pair (tentative, this, ni, &other, other_ni);

        }

      }

#if defined(PRINT_DEBUG_NETCOMPARE)
      tl::info << indent << "finished analysis of ambiguity group with " << nr->num << " members";
#endif

    }

  }

  return new_nodes;
}


// --------------------------------------------------------------------------------------------------------------------
//  NetlistComparer implementation

NetlistComparer::NetlistComparer (NetlistCompareLogger *logger)
  : mp_logger (logger)
{
  mp_device_categorizer.reset (new DeviceCategorizer ());
  mp_circuit_categorizer.reset (new CircuitCategorizer ());
  mp_circuit_pin_mapper.reset (new CircuitPinMapper ());

  m_cap_threshold = -1.0;   //  not set
  m_res_threshold = -1.0;   //  not set

  m_max_depth = 8;
  m_max_n_branch = 100;
}

void
NetlistComparer::exclude_caps (double threshold)
{
  m_cap_threshold = threshold;
}

void
NetlistComparer::exclude_resistors (double threshold)
{
  m_res_threshold = threshold;
}

void
NetlistComparer::same_nets (const db::Net *na, const db::Net *nb)
{
  m_same_nets [std::make_pair (na->circuit (), nb->circuit ())].push_back (std::make_pair (na, nb));
}

void
NetlistComparer::equivalent_pins (const db::Circuit *cb, size_t pin1_id, size_t pin2_id)
{
  mp_circuit_pin_mapper->map_pins (cb, pin1_id, pin2_id);
}

void
NetlistComparer::equivalent_pins (const db::Circuit *cb, const std::vector<size_t> &pin_ids)
{
  mp_circuit_pin_mapper->map_pins (cb, pin_ids);
}

void
NetlistComparer::same_device_classes (const db::DeviceClass *ca, const db::DeviceClass *cb)
{
  tl_assert (ca != 0);
  tl_assert (cb != 0);
  mp_device_categorizer->same_class (ca, cb);
}

void
NetlistComparer::same_circuits (const db::Circuit *ca, const db::Circuit *cb)
{
  tl_assert (ca != 0);
  tl_assert (cb != 0);
  mp_circuit_categorizer->same_circuit (ca, cb);
}

bool
NetlistComparer::compare (const db::Netlist *a, const db::Netlist *b, NetlistCompareLogger *logger) const
{
  db::NetlistCompareLogger *org_logger = mp_logger;
  mp_logger = logger;
  bool res = false;

  try {
    res = compare (a, b);
  } catch (...) {
    mp_logger = org_logger;
    throw;
  }

  mp_logger = org_logger;
  return res;
}

bool
NetlistComparer::compare (const db::Netlist *a, const db::Netlist *b) const
{
  //  we need to create a copy because this method is supposed to be const.
  db::CircuitCategorizer circuit_categorizer = *mp_circuit_categorizer;
  db::DeviceCategorizer device_categorizer = *mp_device_categorizer;
  db::CircuitPinMapper circuit_pin_mapper = *mp_circuit_pin_mapper;

  bool good = true;

  std::map<size_t, std::pair<const db::Circuit *, const db::Circuit *> > cat2circuits;

  for (db::Netlist::const_circuit_iterator i = a->begin_circuits (); i != a->end_circuits (); ++i) {
    size_t cat = circuit_categorizer.cat_for_circuit (i.operator-> ());
    cat2circuits[cat].first = i.operator-> ();
  }

  for (db::Netlist::const_circuit_iterator i = b->begin_circuits (); i != b->end_circuits (); ++i) {
    size_t cat = circuit_categorizer.cat_for_circuit (i.operator-> ());
    cat2circuits[cat].second = i.operator-> ();
  }

  if (mp_logger) {
    mp_logger->begin_netlist (a, b);
  }

  //  check for device classes that don't match

  std::map<size_t, std::pair<const db::DeviceClass *, const db::DeviceClass *> > cat2dc;

  for (db::Netlist::const_device_class_iterator dc = a->begin_device_classes (); dc != a->end_device_classes (); ++dc) {
    size_t cat = device_categorizer.cat_for_device_class (dc.operator-> ());
    cat2dc.insert (std::make_pair (cat, std::make_pair ((const db::DeviceClass *) 0, (const db::DeviceClass *) 0))).first->second.first = dc.operator-> ();
  }

  for (db::Netlist::const_device_class_iterator dc = b->begin_device_classes (); dc != b->end_device_classes (); ++dc) {
    size_t cat = device_categorizer.cat_for_device_class (dc.operator-> ());
    cat2dc.insert (std::make_pair (cat, std::make_pair ((const db::DeviceClass *) 0, (const db::DeviceClass *) 0))).first->second.second = dc.operator-> ();
  }

  for (std::map<size_t, std::pair<const db::DeviceClass *, const db::DeviceClass *> >::const_iterator i = cat2dc.begin (); i != cat2dc.end (); ++i) {
    if (! i->second.first || ! i->second.second) {
      good = false;
      if (mp_logger) {
        mp_logger->device_class_mismatch (i->second.first, i->second.second);
      }
    }
  }

  //  check for circuits that don't match

  for (std::map<size_t, std::pair<const db::Circuit *, const db::Circuit *> >::const_iterator i = cat2circuits.begin (); i != cat2circuits.end (); ++i) {
    if (! i->second.first || ! i->second.second) {
      good = false;
      if (mp_logger) {
        mp_logger->circuit_mismatch (i->second.first, i->second.second);
      }
    }
  }

  std::set<const db::Circuit *> verified_circuits_a, verified_circuits_b;
  std::map<const db::Circuit *, CircuitMapper> c12_pin_mapping, c22_pin_mapping;

  for (db::Netlist::const_bottom_up_circuit_iterator c = a->begin_bottom_up (); c != a->end_bottom_up (); ++c) {

    size_t ccat = circuit_categorizer.cat_for_circuit (c.operator-> ());

    std::map<size_t, std::pair<const db::Circuit *, const db::Circuit *> >::const_iterator i = cat2circuits.find (ccat);
    tl_assert (i != cat2circuits.end ());

    if (i->second.first && i->second.second) {

      const db::Circuit *ca = i->second.first;
      const db::Circuit *cb = i->second.second;

      std::vector<std::pair<const Net *, const Net *> > empty;
      const std::vector<std::pair<const Net *, const Net *> > *net_identity = &empty;
      std::map<std::pair<const db::Circuit *, const db::Circuit *>, std::vector<std::pair<const Net *, const Net *> > >::const_iterator sn = m_same_nets.find (std::make_pair (ca, cb));
      if (sn != m_same_nets.end ()) {
        net_identity = &sn->second;
      }

      if (all_subcircuits_verified (ca, verified_circuits_a) && all_subcircuits_verified (cb, verified_circuits_b)) {

#if defined(PRINT_DEBUG_NETCOMPARE)
        tl::info << "treating circuit: " << ca->name () << " vs. " << cb->name ();
#endif
        if (mp_logger) {
          mp_logger->begin_circuit (ca, cb);
        }

        bool pin_mismatch = false;
        bool g = compare_circuits (ca, cb, device_categorizer, circuit_categorizer, circuit_pin_mapper, *net_identity, pin_mismatch, c12_pin_mapping, c22_pin_mapping);
        if (! g) {
          good = false;
        }

        if (! pin_mismatch) {
          verified_circuits_a.insert (ca);
          verified_circuits_b.insert (cb);
        }

        derive_pin_equivalence (ca, cb, &circuit_pin_mapper);

        if (mp_logger) {
          mp_logger->end_circuit (ca, cb, g);
        }

      } else {

        if (mp_logger) {
          mp_logger->circuit_skipped (ca, cb);
          good = false;
        }

      }

    }

  }

  if (mp_logger) {
    mp_logger->end_netlist (a, b);
  }

  return good;
}

static
std::vector<size_t> collect_pins_with_empty_nets (const db::Circuit *c, CircuitPinMapper *circuit_pin_mapper)
{
  std::vector<size_t> pins;

  for (db::Circuit::const_pin_iterator p = c->begin_pins (); p != c->end_pins (); ++p) {
    const db::Net *net = c->net_for_pin (p->id ());
    if ((! net || net->is_floating ()) && ! circuit_pin_mapper->is_mapped (c, p->id ())) {
      pins.push_back (p->id ());
    }
  }

  return pins;
}

void
NetlistComparer::derive_pin_equivalence (const db::Circuit *ca, const db::Circuit *cb, CircuitPinMapper *circuit_pin_mapper)
{
  //  TODO: All pins with empty nets are treated as equivalent - this as a quick way to
  //  treat circuits abstracts, although it's not really valid. By doing this, we
  //  don't capture the case of multiple (abstract) subcircuits wired in different ways.

  std::vector<size_t> pa, pb;
  pa = collect_pins_with_empty_nets (ca, circuit_pin_mapper);
  pb = collect_pins_with_empty_nets (cb, circuit_pin_mapper);

  circuit_pin_mapper->map_pins (ca, pa);
  circuit_pin_mapper->map_pins (cb, pb);
}

bool
NetlistComparer::all_subcircuits_verified (const db::Circuit *c, const std::set<const db::Circuit *> &verified_circuits) const
{
  for (db::Circuit::const_subcircuit_iterator sc = c->begin_subcircuits (); sc != c->end_subcircuits (); ++sc) {

    const db::Circuit *cr = sc->circuit_ref ();

    //  typical via subcircuits attach through one pin. We can safely ignore such subcircuits because they don't
    //  contribute graph edges.
    if (cr->pin_count () > 1 && verified_circuits.find (cr) == verified_circuits.end ()) {
      return false;
    }

  }

  return true;
}

static std::vector<std::pair<size_t, size_t> >
compute_device_key (const db::Device &device, const db::NetGraph &g)
{
  std::vector<std::pair<size_t, size_t> > k;

  const std::vector<db::DeviceTerminalDefinition> &td = device.device_class ()->terminal_definitions ();
  for (std::vector<db::DeviceTerminalDefinition>::const_iterator t = td.begin (); t != td.end (); ++t) {
    size_t terminal_id = translate_terminal_id (t->id (), &device);
    const db::Net *net = device.net_for_terminal (t->id ());
    size_t net_id = g.node_index_for_net (net);
    k.push_back (std::make_pair (terminal_id, net_id));
  }

  std::sort (k.begin (), k.end ());

  return k;
}

static std::vector<std::pair<size_t, size_t> >
compute_subcircuit_key (const db::SubCircuit &subcircuit, const db::NetGraph &g, const std::map<const db::Circuit *, CircuitMapper> *circuit_map, const CircuitPinMapper *pin_map)
{
  std::vector<std::pair<size_t, size_t> > k;

  const db::Circuit *cr = subcircuit.circuit_ref ();

  std::map<const db::Circuit *, CircuitMapper>::const_iterator icm = circuit_map->find (cr);
  if (icm == circuit_map->end ()) {
    //  this can happen if the other circuit does not exist - in this case the key is an invalid one which cannot
    //  be produced by a regular subcircuit.
    return k;
  }

  const CircuitMapper *cm = & icm->second;
  cr = cm->other ();

  //  NOTE: cr is given in terms of the canonical "other" circuit.

  for (db::Circuit::const_pin_iterator p = cr->begin_pins (); p != cr->end_pins (); ++p) {

    if (cm->has_this_pin_for_other_pin (p->id ())) {

      size_t this_pin_id = cm->this_pin_from_other_pin (p->id ());
      size_t pin_id = pin_map->normalize_pin_id (cr, p->id ());

      const db::Net *net = subcircuit.net_for_pin (this_pin_id);
      size_t net_id = g.node_index_for_net (net);
      k.push_back (std::make_pair (pin_id, net_id));

    }

  }

  std::sort (k.begin (), k.end ());

  return k;
}

namespace {

inline double size_dist (size_t a, size_t b)
{
  double d = a - b;
  return d * d;
}

struct KeyDistance
{
  typedef std::pair<std::vector<std::pair<size_t, size_t> >, const db::SubCircuit *> value_type;

  double operator() (const value_type &a, const value_type &b) const
  {
    tl_assert (a.first.size () == b.first.size ());
    double d = 0.0;
    for (std::vector<std::pair<size_t, size_t> >::const_iterator i = a.first.begin (), j = b.first.begin (); i != a.first.end (); ++i, ++j) {
      d += size_dist (i->first, j->first) + size_dist (i->second, j->second);
    }
    return d;
  }
};

struct KeySize
{
  typedef std::pair<std::vector<std::pair<size_t, size_t> >, const db::SubCircuit *> value_type;

  bool operator() (const value_type &a, const value_type &b) const
  {
    return (a.first.size () < b.first.size ());
  }
};

template <class Iter, class Distance>
void align (Iter i1, Iter i2, Iter j1, Iter j2, Distance distance)
{
  //  TODO: this can probably be done more efficiently

  std::vector<Iter> vi, vj;
  vi.reserve (std::max (i2 - i1, j2 - j1));
  vj.reserve (std::max (i2 - i1, j2 - j1));

  for (Iter i = i1; i != i2; ++i) {
    vi.push_back (i);
  }

  for (Iter j = j1; j != j2; ++j) {
    vj.push_back (j);
  }

  while (vi.size () < vj.size ()) {
    vi.push_back (Iter ());
  }

  while (vj.size () < vi.size ()) {
    vj.push_back (Iter ());
  }

  if (vi.size () <= 1) {
    return;
  }

  //  Caution: this is an O(2) algorithm ...
  bool any_swapped = true;
  for (size_t n = 0; n < vi.size () - 1 && any_swapped; ++n) {

    any_swapped = false;

    for (size_t m = n + 1; m < vj.size () - 1; ++m) {
      if (vi [n] == Iter () || vi [m] == Iter () || vj [n] == Iter () || vj [m] == Iter ()) {
        continue;
      } else if (distance (*vi [n], *vj [m]) + distance (*vi [m], *vj [n]) < distance (*vi [n], *vj [n]) + distance (*vi [m], *vj [m])) {
        //  this will reduce the overall distance:
        std::swap (vj [n], vj [m]);
        any_swapped = true;
      }
    }

  }
}

}

bool
NetlistComparer::compare_circuits (const db::Circuit *c1, const db::Circuit *c2, db::DeviceCategorizer &device_categorizer, db::CircuitCategorizer &circuit_categorizer, db::CircuitPinMapper &circuit_pin_mapper, const std::vector<std::pair<const Net *, const Net *> > &net_identity, bool &pin_mismatch, std::map<const db::Circuit *, CircuitMapper> &c12_circuit_and_pin_mapping, std::map<const db::Circuit *, CircuitMapper> &c22_circuit_and_pin_mapping) const
{
  db::DeviceFilter device_filter (m_cap_threshold, m_res_threshold);

  db::NetGraph g1, g2;

  //  NOTE: for normalization we map all subcircuits of c1 to c2.
  //  Also, pin swapping will only happen there.
  g1.build (c1, device_categorizer, circuit_categorizer, device_filter, &c12_circuit_and_pin_mapping, &circuit_pin_mapper);
  g2.build (c2, device_categorizer, circuit_categorizer, device_filter, &c22_circuit_and_pin_mapping, &circuit_pin_mapper);

  //  Match dummy nodes for null nets
  g1.identify (0, 0);
  g2.identify (0, 0);

  for (std::vector<std::pair<const Net *, const Net *> >::const_iterator p = net_identity.begin (); p != net_identity.end (); ++p) {
    size_t ni1 = g1.node_index_for_net (p->first);
    size_t ni2 = g2.node_index_for_net (p->second);
    g1.identify (ni1, ni2);
    g2.identify (ni2, ni1);
  }

#if defined(PRINT_DEBUG_NETCOMPARE)
  int iter = 0;
#endif

  //  two passes: one without ambiguities, the second one with

  bool good;

  for (int pass = 0; pass < 2; ++pass) {

#if defined(PRINT_DEBUG_NETCOMPARE)
    if (pass > 0) {
      tl::info << "including ambiguous nodes now.";
    }
#endif

    good = true;
    while (true) {

#if defined(PRINT_DEBUG_NETCOMPARE)
      ++iter;
      tl::info << "new compare iteration #" << iter;
      tl::info << "deducing from present nodes ...";
#endif

      size_t new_identities = 0;

      for (db::NetGraph::node_iterator i1 = g1.begin (); i1 != g1.end (); ++i1) {
        if (i1->has_other () && i1->net ()) {
          size_t ni = g1.derive_node_identities (i1 - g1.begin (), g2, 0, m_max_depth, 1, m_max_n_branch, mp_logger, &circuit_pin_mapper, 0 /*not tentative*/, pass > 0 /*with ambiguities*/);
          if (ni > 0 && ni != std::numeric_limits<size_t>::max ()) {
            new_identities += ni;
#if defined(PRINT_DEBUG_NETCOMPARE)
            tl::info << ni << " new identities.";
#endif
          }
        }
      }

#if defined(PRINT_DEBUG_NETCOMPARE)
      tl::info << "checking topological identity ...";
#endif

      //  derive new identities through topology: first collect all nets with the same topological signature

      std::vector<const NetGraphNode *> nodes, other_nodes;

      nodes.reserve (g1.end () - g1.begin ());
      for (db::NetGraph::node_iterator i1 = g1.begin (); i1 != g1.end (); ++i1) {
        if (! i1->has_other () && i1->net ()) {
          nodes.push_back (i1.operator-> ());
        }
      }

      other_nodes.reserve (g2.end () - g2.begin ());
      for (db::NetGraph::node_iterator i2 = g2.begin (); i2 != g2.end (); ++i2) {
        if (! i2->has_other () && i2->net ()) {
          other_nodes.push_back (i2.operator-> ());
        }
      }

      if (nodes.empty () || other_nodes.empty ()) {
        if (! nodes.empty () || ! other_nodes.empty ()) {
          good = false;
        }
        //  this assumes that we don't gain anything here. Stop now.
        break;
      }

      std::sort (nodes.begin (), nodes.end (), CompareNodePtr ());
      std::sort (other_nodes.begin (), other_nodes.end (), CompareNodePtr ());

      size_t ni = g1.derive_node_identities_from_node_set (nodes, other_nodes, g2, 0, m_max_depth, 1, m_max_n_branch, mp_logger, &circuit_pin_mapper, 0 /*not tentative*/, pass > 0 /*with ambiguities*/);
      if (ni > 0 && ni != std::numeric_limits<size_t>::max ()) {
        new_identities += ni;
#if defined(PRINT_DEBUG_NETCOMPARE)
        tl::info << ni << " new identities.";
#endif
      }

      if (new_identities == 0) {
        good = false;
        break;
      }

    }

  }

  //  Report missing net assignment

  for (db::NetGraph::node_iterator i = g1.begin (); i != g1.end (); ++i) {
    if (! i->has_other () && mp_logger) {
      mp_logger->net_mismatch (i->net (), 0);
    }
  }

  for (db::NetGraph::node_iterator i = g2.begin (); i != g2.end (); ++i) {
    if (! i->has_other () && mp_logger) {
      mp_logger->net_mismatch (0, i->net ());
    }
  }


  //  Report pin assignment
  //  This step also does the pin identity mapping.

  if (c1->pin_count () > 0 && c2->pin_count () > 0) {

    std::vector<const db::Pin *> floating_pins;
    std::multimap<size_t, const db::Pin *> net2pin;
    for (db::Circuit::const_pin_iterator p = c2->begin_pins (); p != c2->end_pins (); ++p) {
      const db::Net *net = c2->net_for_pin (p->id ());
      if (net) {
        net2pin.insert (std::make_pair (g2.node_index_for_net (net), p.operator-> ()));
      } else {
        floating_pins.push_back (p.operator-> ());
      }
    }

    std::vector<const db::Pin *>::iterator next_float = floating_pins.begin ();

    CircuitMapper &c12_pin_mapping = c12_circuit_and_pin_mapping [c1];
    c12_pin_mapping.set_other (c2);

    //  dummy mapping: we show this circuit is used.
    CircuitMapper &c22_pin_mapping = c22_circuit_and_pin_mapping [c2];
    c22_pin_mapping.set_other (c2);

    for (db::Circuit::const_pin_iterator p = c1->begin_pins (); p != c1->end_pins (); ++p) {

      const db::Net *net = c1->net_for_pin (p->id ());
      if (! net) {

        if (next_float != floating_pins.end ()) {

          //  assign a floating pin - this is a dummy assignment which is mitigated
          //  by declaring the pins equivalent in derive_pin_equivalence
          if (mp_logger) {
            mp_logger->match_pins (p.operator-> (), *next_float);
          }
          c12_pin_mapping.map_pin (p->id (), (*next_float)->id ());
          c22_pin_mapping.map_pin ((*next_float)->id (), p->id ());

          ++next_float;

        } else {

          //  otherwise this is an error
          if (mp_logger) {
            mp_logger->pin_mismatch (p.operator-> (), 0);
          }

          pin_mismatch = true;
          good = false;

        }

        continue;

      }

      const db::NetGraphNode &n = *(g1.begin () + g1.node_index_for_net (net));

      if (! n.has_other ()) {

        if (mp_logger) {
          mp_logger->pin_mismatch (p.operator-> (), 0);
        }

        pin_mismatch = true;
        good = false;

        continue;

      }

      std::multimap<size_t, const db::Pin *>::iterator np = net2pin.find (n.other_net_index ());
      for (db::Net::const_pin_iterator pi = net->begin_pins (); pi != net->end_pins (); ++pi) {

        if (np != net2pin.end () && np->first == n.other_net_index ()) {

          if (mp_logger) {
            mp_logger->match_pins (pi->pin (), np->second);
          }
          c12_pin_mapping.map_pin (pi->pin ()->id (), np->second->id ());
          //  dummy mapping: we show this pin is used.
          c22_pin_mapping.map_pin (np->second->id (), np->second->id ());

          std::multimap<size_t, const db::Pin *>::iterator np_delete = np;
          ++np;
          net2pin.erase (np_delete);

        } else {

          if (mp_logger) {
            mp_logger->pin_mismatch (pi->pin (), 0);
          }
          pin_mismatch = true;
          good = false;

        }

      }

    }

    for (std::multimap<size_t, const db::Pin *>::iterator np = net2pin.begin (); np != net2pin.end (); ++np) {
      if (mp_logger) {
        mp_logger->pin_mismatch (0, np->second);
      }
      pin_mismatch = true;
      good = false;
    }

    while (next_float != floating_pins.end ()) {
      if (mp_logger) {
        mp_logger->pin_mismatch (0, *next_float);
      }
      pin_mismatch = true;
      good = false;
      ++next_float;
    }

  } else {

    //  skip pin mapping in case one circuit does not feature pins
    //  This is often the case for top-level circuits. We don't necessarily need pins for them.
    //  We still report those circuits with "pin mismatch" so they don't get considered within
    //  subcircuits. Plus we report the pins so they get listed in the cross-ref (but with a
    //  "match" - this is important to cover the cases which are found when analyzing the nets).

    if (mp_logger) {
      for (db::Circuit::const_pin_iterator p = c1->begin_pins (); p != c1->end_pins (); ++p) {
        mp_logger->match_pins (p.operator-> (), 0);
      }
      for (db::Circuit::const_pin_iterator p = c2->begin_pins (); p != c2->end_pins (); ++p) {
        mp_logger->match_pins (0, p.operator-> ());
      }
    }

    if (c1->pin_count () != c2->pin_count ()) {
      pin_mismatch = true;
    }

  }

  //  Report device assignment

  std::multimap<std::vector<std::pair<size_t, size_t> >, std::pair<const db::Device *, size_t> > device_map;

  for (db::Circuit::const_device_iterator d = c1->begin_devices (); d != c1->end_devices (); ++d) {

    if (! device_filter.filter (d.operator-> ())) {
      continue;
    }

    std::vector<std::pair<size_t, size_t> > k = compute_device_key (*d, g1);

    bool mapped = true;
    for (std::vector<std::pair<size_t, size_t> >::iterator i = k.begin (); i != k.end () && mapped; ++i) {
      if (! g1.begin () [i->second].has_other ()) {
        mapped = false;
      }
    }

    if (! mapped) {
      if (mp_logger) {
        mp_logger->device_mismatch (d.operator-> (), 0);
      }
      good = false;
    } else {
      //  TODO: report devices which cannot be distiguished topologically?
      device_map.insert (std::make_pair (k, std::make_pair (d.operator-> (), device_categorizer.cat_for_device (d.operator-> ()))));
    }

  }

  for (db::Circuit::const_device_iterator d = c2->begin_devices (); d != c2->end_devices (); ++d) {

    if (! device_filter.filter (d.operator-> ())) {
      continue;
    }

    std::vector<std::pair<size_t, size_t> > k = compute_device_key (*d, g2);

    bool mapped = true;
    for (std::vector<std::pair<size_t, size_t> >::iterator i = k.begin (); i != k.end (); ++i) {
      if (! g2.begin () [i->second].has_other ()) {
        mapped = false;
      } else {
        i->second = g2.begin () [i->second].other_net_index ();
      }
    }

    std::sort (k.begin (), k.end ());

    std::multimap<std::vector<std::pair<size_t, size_t> >, std::pair<const db::Device *, size_t> >::iterator dm = device_map.find (k);

    if (! mapped || dm == device_map.end () || dm->first != k) {

      if (mp_logger) {
        mp_logger->device_mismatch (0, d.operator-> ());
      }
      good = false;

    } else {

      db::DeviceCompare dc;

      size_t device_cat = device_categorizer.cat_for_device (d.operator-> ());

      if (! dc.equals (dm->second, std::make_pair (d.operator-> (), device_cat))) {
        if (dm->second.second != device_cat) {
          if (mp_logger) {
            mp_logger->match_devices_with_different_device_classes (dm->second.first, d.operator-> ());
          }
          good = false;
        } else {
          if (mp_logger) {
            mp_logger->match_devices_with_different_parameters (dm->second.first, d.operator-> ());
          }
          good = false;
        }
      } else {
        if (mp_logger) {
          mp_logger->match_devices (dm->second.first, d.operator-> ());
        }
      }

      device_map.erase (dm);

    }

  }

  for (std::multimap<std::vector<std::pair<size_t, size_t> >, std::pair<const db::Device *, size_t> >::const_iterator dm = device_map.begin (); dm != device_map.end (); ++dm) {
    if (mp_logger) {
      mp_logger->device_mismatch (dm->second.first, 0);
    }
    good = false;
  }


  //  Report subcircuit assignment

  std::multimap<std::vector<std::pair<size_t, size_t> >, std::pair<const db::SubCircuit *, size_t> > subcircuit_map;

  for (db::Circuit::const_subcircuit_iterator sc = c1->begin_subcircuits (); sc != c1->end_subcircuits (); ++sc) {

    std::vector<std::pair<size_t, size_t> > k = compute_subcircuit_key (*sc, g1, &c12_circuit_and_pin_mapping, &circuit_pin_mapper);

    bool mapped = true;
    for (std::vector<std::pair<size_t, size_t> >::iterator i = k.begin (); i != k.end () && mapped; ++i) {
      if (! g1.begin () [i->second].has_other ()) {
        mapped = false;
      }
    }

    if (! mapped) {
      if (mp_logger) {
        mp_logger->subcircuit_mismatch (sc.operator-> (), 0);
      }
      good = false;
    } else if (! k.empty ()) {
      //  TODO: report devices which cannot be distiguished topologically?
      subcircuit_map.insert (std::make_pair (k, std::make_pair (sc.operator-> (), circuit_categorizer.cat_for_subcircuit (sc.operator-> ()))));
    }

  }

  typedef std::vector<std::pair<std::vector<std::pair<size_t, size_t> >, const db::SubCircuit *> > unmatched_list;
  unmatched_list unmatched_a, unmatched_b;

  for (db::Circuit::const_subcircuit_iterator sc = c2->begin_subcircuits (); sc != c2->end_subcircuits (); ++sc) {

    std::vector<std::pair<size_t, size_t> > k = compute_subcircuit_key (*sc, g2, &c22_circuit_and_pin_mapping, &circuit_pin_mapper);

    bool mapped = true;
    for (std::vector<std::pair<size_t, size_t> >::iterator i = k.begin (); i != k.end (); ++i) {
      if (! g1.begin () [i->second].has_other ()) {
        mapped = false;
      } else {
        i->second = g2.begin () [i->second].other_net_index ();
      }
    }

    std::sort (k.begin (), k.end ());

    std::multimap<std::vector<std::pair<size_t, size_t> >, std::pair<const db::SubCircuit *, size_t> >::iterator scm = subcircuit_map.find (k);

    if (! mapped || scm == subcircuit_map.end ()) {

      if (mp_logger) {
        unmatched_b.push_back (std::make_pair (k, sc.operator-> ()));
      }
      good = false;

    } else {

      db::SubCircuitCompare scc;
      size_t sc_cat = circuit_categorizer.cat_for_subcircuit (sc.operator-> ());

      if (! scc.equals (scm->second, std::make_pair (sc.operator-> (), sc_cat))) {
        if (mp_logger) {
          mp_logger->subcircuit_mismatch (scm->second.first, sc.operator-> ());
        }
        good = false;
      } else {
        if (mp_logger) {
          mp_logger->match_subcircuits (scm->second.first, sc.operator-> ());
        }
      }

      subcircuit_map.erase (scm);

    }

  }

  for (std::multimap<std::vector<std::pair<size_t, size_t> >, std::pair<const db::SubCircuit *, size_t> >::const_iterator scm = subcircuit_map.begin (); scm != subcircuit_map.end (); ++scm) {
    if (mp_logger) {
      unmatched_a.push_back (std::make_pair (scm->first, scm->second.first));
    }
    good = false;
  }

  //  try to do some pairing between the mismatching subcircuits - even though we will still report them as
  //  mismatches it will give some better hint about what needs to be fixed

  if (mp_logger) {

    size_t max_analysis_set = 1000;
    if (unmatched_a.size () + unmatched_b.size () > max_analysis_set) {

      //  don't try too much analysis - this may be a waste of time
      for (unmatched_list::const_iterator i = unmatched_a.begin (); i != unmatched_a.end (); ++i) {
        mp_logger->subcircuit_mismatch (i->second, 0);
      }
      for (unmatched_list::const_iterator i = unmatched_b.begin (); i != unmatched_b.end (); ++i) {
        mp_logger->subcircuit_mismatch (0, i->second);
      }

    } else {

      std::sort (unmatched_a.begin (), unmatched_a.end (), KeySize ());
      std::sort (unmatched_b.begin (), unmatched_b.end (), KeySize ());

      for (unmatched_list::iterator i = unmatched_a.begin (), j = unmatched_b.begin (); i != unmatched_a.end () || j != unmatched_b.end (); ) {

        while (j != unmatched_b.end () && (i == unmatched_a.end () || j->first.size () < i->first.size ())) {
          mp_logger->subcircuit_mismatch (0, j->second);
          ++j;
        }

        while (i != unmatched_a.end () && (j == unmatched_b.end () || i->first.size () < j->first.size ())) {
          mp_logger->subcircuit_mismatch (i->second, 0);
          ++i;
        }

        if (i == unmatched_a.end () && j == unmatched_b.end ()) {
          break;
        }

        unmatched_list::iterator ii = i, jj = j;
        ++i, ++j;
        size_t n = ii->first.size ();
        tl_assert (n == jj->first.size ());

        while (i != unmatched_a.end () && i->first.size () == n) {
          ++i;
        }

        while (j != unmatched_b.end () && j->first.size () == n) {
          ++j;
        }

        align (ii, i, jj, j, KeyDistance ());

        for ( ; ii != i && jj != j; ++ii, ++jj) {
          mp_logger->subcircuit_mismatch (ii->second, jj->second);
        }

        for ( ; jj != j; ++jj) {
          mp_logger->subcircuit_mismatch (0, jj->second);
        }

        for ( ; ii != i; ++ii) {
          mp_logger->subcircuit_mismatch (ii->second, 0);
        }

      }

    }

  }

  return good;
}

}
