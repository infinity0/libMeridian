Measurement closest(double beta, Node t[]) {
	Measurement self = get_distance_tcp(t, -1);
	double avg_lat = array_avg(self.distance);
	double min_lat = array_min(self.distance);
	double max_lat = array_max(self.distance);
	Node ring_m[] = array_intersect(
		ring_ge((1.0 - beta) * min_lat), ring_le((1.0 + beta) * max_lat));
	if (array_size(ring_m) == 0) {
		return self;
	}
	Measurement r_lat[] = get_distance_tcp(ring_m, t, 
		ceil((2.0 * beta + 1.0) * max_lat));
	int min_index = -1; 
	min_lat = avg_lat;
	for (int i = 0; i < array_size(r_lat); i = i + 1) { 
		double cur_lat = array_avg(r_lat[i].distance);
		if (cur_lat < min_lat) {
			min_index = i;
			min_lat = cur_lat; 
		}
	}
	if (min_index == -1) {
		return self;
	}
	Measurement min_n = r_lat[min_index];	
	if (min_n.addr != 0 && min_lat < (avg_lat * beta)) {
		Measurement ret_n = rpc(ring_m[min_index], closest, beta, t);
		if (ret_n.addr != 0) {
			return ret_n;
		}
	}	
	return min_n;
}

int main() {
	Node t1 = {dns_lookup("www.cs.berkeley.edu"), 80, 0, 0};
	Node ts[] = {t1};
	Node e = {dns_lookup("planetlab1.cs.cornell.edu"), 3964, 0, 0};	
	Measurement r = rpc(e, closest, 0.5, ts);
	if (r.addr == 0) {
		r.addr = e.addr;
		r.port = e.port;
	}
	print("Closest node is ");
	println(dns_addr(r.addr));
	for (int i = 0; i < array_size(r.distance); i = i + 1) {
		print("--> ");
		print(r.distance[i]);
		print(" from ");
		println(dns_addr(ts[i].addr));
	}
	return 0;
}
