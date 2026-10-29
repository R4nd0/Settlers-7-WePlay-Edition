// Local integration fixture only. The deployment entry remains src/worker.mjs.
import worker,{updateBoard} from "../src/worker.mjs";
export default {
  async fetch(request,env,ctx){
    if(new URL(request.url).pathname==="/__test_board" && request.method==="POST"){
      await updateBoard(env);return new Response("checked");
    }
    return worker.fetch(request,env,ctx);
  }
};
