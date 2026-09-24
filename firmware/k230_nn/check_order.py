"""Raw kmodel output order and shapes for one real-wet sample (no re-ordering)."""
import numpy as np
import nncase
km = open("build/leak_f32_uint8/leak_f32_uint8.kmodel", "rb").read()
sim = nncase.Simulator()
sim.load_model(km)
x = np.load("data/test.npy")[:1].astype(np.float32)
sim.set_input_tensor(0, nncase.RuntimeTensor.from_numpy(x))
sim.run()
for k in range(sim.outputs_size):
    o = sim.get_output_tensor(k).to_numpy()
    print("output", k, o.shape, o.dtype, o.ravel())
print("TF: sev 0.545, inop 0.000")
