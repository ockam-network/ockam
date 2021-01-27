fn main() {
    let executor = NodeExecutor::new();
    let context = executor.new_worker_context();

    executor.execute(async move {
        context.node().stop().await.unwrap();
    }).unwrap();
}
