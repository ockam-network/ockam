extern crate alloc;
extern crate simple_redis;

use alloc::rc::Rc;
use core::cell::RefCell;
use ockam_queue_topic::queue::*;
use ockam_queue_topic::topic::MemTopicWorker;
use simple_redis::client::Client;
use simple_redis::RedisResult;
use std::str::FromStr;

pub struct RedisQueue {
    address: String,
    client: Rc<RefCell<Client>>,
}

pub struct RedisQueueWorker {
    client: Rc<RefCell<Client>>,
}

impl RedisQueueWorker {
    pub fn create(url: &str) -> Option<QueueWorkerHandle> {
        match simple_redis::create(url) {
            Ok(client) => Some(Rc::new(RefCell::new(RedisQueueWorker {
                client: Rc::new(RefCell::new(client)),
            }))),
            Err(_) => None,
        }
    }
}

impl QueueManagement for RedisQueueWorker {
    fn address(&self) -> String {
        unimplemented!()
    }

    fn get_queue(&mut self, queue_address: &str) -> Option<QueueHandle> {
        Some(Rc::new(RefCell::new(RedisQueue {
            client: self.client.clone(),
            address: queue_address.to_string(),
        })))
    }

    fn remove_queue(&mut self, queue_name: &str) {
        match self.client.borrow_mut().ltrim(queue_name, -1, 0) {
            Err(e) => {
                println!("Redis remove failed: {}", e)
            }
            _ => (),
        }
    }
}

pub fn main() {
    let queue_worker = RedisQueueWorker::create("redis://127.0.0.1:6379/").unwrap();
    let topic_worker = MemTopicWorker::create(queue_worker);

    let mut tw = topic_worker.borrow_mut();
    let sub = tw.subscribe("test").unwrap();

    tw.publish("test", QueueMessage::from_str("ockam").unwrap());

    tw.publish("test", QueueMessage::from_str("ockam!").unwrap());

    let messages = tw.consume_messages(&sub);

    assert_eq!(2, messages.len());
    tw.unsubscribe(&sub);
}
